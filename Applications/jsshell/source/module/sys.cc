// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "module/sys.h"

#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "object_inspector.h"
#include "perception/clipboard.h"
#include "perception/devices/storage_device.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/power.h"
#include "perception/processes.h"
#include "perception/registry.h"
#include "perception/registry_service.h"
#include "perception/serialization/value.h"
#include "perception/services.h"
#include "perception/shared_memory_pipe.h"
#include "perception/storage_manager.h"
#include "perception/time.h"

extern char** environ;

namespace perception {
std::shared_ptr<SharedMemoryPipe> GetFileDescriptorPipe(int fd);
}  // namespace perception

namespace module {
namespace {

// Default terminal column width when ioctl size query is unavailable.
constexpr int kDefaultTermCols = 80;

// Default terminal row height when ioctl size query is unavailable.
constexpr int kDefaultTermRows = 24;

// Polling interval in milliseconds when waiting for terminal input.
constexpr int kKeyPollIntervalMs = 5;

// Maximum CPU core percentages queried from GetProcessHealthMetrics.
constexpr size_t kMaxCpuCoreMetrics = 64;

// Microseconds per millisecond conversion factor.
constexpr double kMicrosecondsPerMillisecond = 1000.0;

// ANSI escape sequence to clear screen and home the cursor.
constexpr std::string_view kAnsiClearScreen = "\x1b[2J\x1b[H";

// ANSI escape sequence to hide the terminal cursor.
constexpr std::string_view kAnsiHideCursor = "\x1b[?25l";

// ANSI escape sequence to show the terminal cursor.
constexpr std::string_view kAnsiShowCursor = "\x1b[?25h";

// ANSI escape sequence to enter the alternate screen buffer.
constexpr std::string_view kAnsiAltScreenEnter = "\x1b[?1049h";

// ANSI escape sequence to leave the alternate screen buffer.
constexpr std::string_view kAnsiAltScreenLeave = "\x1b[?1049l";

// ANSI escape sequence to enable Kitty keyboard protocol flags.
constexpr std::string_view kAnsiKittyKbdEnter = "\x1b[>1u";

// ANSI escape sequence to disable Kitty keyboard protocol flags.
constexpr std::string_view kAnsiKittyKbdLeave = "\x1b[<u";

// ANSI escape sequence to enable SGR mouse reporting.
constexpr std::string_view kAnsiMouseEnter = "\x1b[?1000h\x1b[?1006h";

// ANSI escape sequence to disable SGR mouse reporting.
constexpr std::string_view kAnsiMouseLeave = "\x1b[?1000l\x1b[?1006l";

// ANSI reset sequence.
constexpr std::string_view kAnsiReset = "\x1b[0m";

// Queue of raw terminal bytes read from stdin awaiting key token decoding.
std::deque<unsigned char>& PendingKeyBytes() {
  static std::deque<unsigned char> queue;
  return queue;
}

// Active console.time / console.timeEnd timers keyed by label.
std::unordered_map<std::string, std::chrono::microseconds>& ConsoleTimers() {
  static std::unordered_map<std::string, std::chrono::microseconds> timers;
  return timers;
}

JsEngine* GetEngine(JSContext* ctx) {
  return static_cast<JsEngine*>(JS_GetContextOpaque(ctx));
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    char ca = a[i];
    char cb = b[i];
    if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
    if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
    if (ca != cb) return false;
  }
  return true;
}

bool IsCorpusName(std::string_view s) {
  return EqualsIgnoreCase(s, "applications") ||
         EqualsIgnoreCase(s, "libraries") || EqualsIgnoreCase(s, "app") ||
         EqualsIgnoreCase(s, "lib");
}

perception::RegistryCorpus ParseCorpus(std::string_view s) {
  if (EqualsIgnoreCase(s, "libraries") || EqualsIgnoreCase(s, "lib"))
    return perception::RegistryCorpus::LIBRARIES;
  return perception::RegistryCorpus::APPLICATIONS;
}

std::string CorpusToString(perception::RegistryCorpus corpus) {
  return corpus == perception::RegistryCorpus::LIBRARIES ? "libraries"
                                                         : "applications";
}

// Converts a Perception serialization::Value into a QuickJS JSValue.
JSValue SerializationValueToJs(JSContext* ctx,
                               const perception::serialization::Value& val) {
  using Type = perception::serialization::Value::Type;
  switch (val.GetType()) {
    case Type::UNDEFINED:
      return JS_UNDEFINED;
    case Type::BOOLEAN:
      return JS_NewBool(ctx, val.BoolValue().value_or(false) ? 1 : 0);
    case Type::INTEGER:
      return JS_NewInt64(ctx, val.IntegerValue().value_or(0));
    case Type::FLOAT:
      return JS_NewFloat64(ctx, val.FloatValue().value_or(0.0));
    case Type::COLOR_RGB:
      return JS_NewUint32(ctx, val.ColorRGBValue().value_or(0));
    case Type::STRING: {
      std::string_view sv = val.StringValue().value_or("");
      return JS_NewStringLen(ctx, sv.data(), sv.size());
    }
    case Type::ARRAY: {
      JSValue arr = JS_NewArray(ctx);
      const auto* vec = val.ArrayValue();
      if (vec != nullptr) {
        for (size_t i = 0; i < vec->size(); ++i) {
          JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                               SerializationValueToJs(ctx, (*vec)[i]));
        }
      }
      return arr;
    }
  }
  return JS_UNDEFINED;
}

// Converts a QuickJS JSValue into a Perception serialization::Value.
perception::serialization::Value JsToSerializationValue(JSContext* ctx,
                                                        JSValueConst val) {
  using Value = perception::serialization::Value;
  if (JS_IsUndefined(val) || JS_IsNull(val)) return Value();
  if (JS_IsBool(val)) return Value(JS_ToBool(ctx, val) != 0);
  if (JS_IsNumber(val)) {
    int tag = JS_VALUE_GET_TAG(val);
    if (tag == JS_TAG_INT) {
      int64_t iv = 0;
      JS_ToInt64(ctx, &iv, val);
      return Value(static_cast<int64>(iv));
    }
    double dv = 0.0;
    JS_ToFloat64(ctx, &dv, val);
    int64_t iv = static_cast<int64_t>(dv);
    if (static_cast<double>(iv) == dv) return Value(static_cast<int64>(iv));
    return Value(dv);
  }
  if (JS_IsString(val)) {
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx, &len, val);
    if (cstr == nullptr) return Value(std::string_view(""));
    Value res(std::string_view(cstr, len));
    JS_FreeCString(ctx, cstr);
    return res;
  }
  size_t byte_offset = 0;
  size_t byte_len = 0;
  size_t bytes_per_elem = 0;
  JSValue ab =
      JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_len, &bytes_per_elem);
  if (!JS_IsException(ab)) {
    size_t ab_size = 0;
    uint8_t* ptr = JS_GetArrayBuffer(ctx, &ab_size, ab);
    std::string raw;
    if (ptr != nullptr && byte_offset + byte_len <= ab_size) {
      raw.assign(reinterpret_cast<const char*>(ptr + byte_offset), byte_len);
    }
    JS_FreeValue(ctx, ab);
    return Value(std::move(raw));
  }
  JS_FreeValue(ctx, JS_GetException(ctx));

  if (JS_IsArray(ctx, val)) {
    JSValue len_val = JS_GetPropertyStr(ctx, val, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    std::vector<Value> elems;
    elems.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
      JSValue item = JS_GetPropertyUint32(ctx, val, i);
      elems.push_back(JsToSerializationValue(ctx, item));
      JS_FreeValue(ctx, item);
    }
    return Value(std::move(elems));
  }
  if (JS_IsObject(val)) {
    JSValue json_val = JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(json_val) && JS_IsString(json_val)) {
      size_t len = 0;
      const char* cstr = JS_ToCStringLen(ctx, &len, json_val);
      std::string s = (cstr != nullptr) ? std::string(cstr, len) : "{}";
      if (cstr != nullptr) JS_FreeCString(ctx, cstr);
      JS_FreeValue(ctx, json_val);
      return Value(std::move(s));
    }
    if (JS_IsException(json_val)) JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, json_val);
  }
  const char* cstr = JS_ToCString(ctx, val);
  if (cstr == nullptr) return Value();
  Value res{std::string_view(cstr)};
  JS_FreeCString(ctx, cstr);
  return res;
}

// Wraps a synchronous JSValue result in a resolved Promise so callers can use
// either synchronous property access or `await`.
JSValue MakeResolvedPromise(JSContext* ctx, JSValue val) {
  if (JS_IsException(val)) return val;
  JSValue resolving_funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
  JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
  if (JS_IsException(promise)) {
    JS_FreeValue(ctx, val);
    return promise;
  }
  JSValue ret = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &val);
  JS_FreeValue(ctx, ret);
  JS_FreeValue(ctx, val);
  JS_FreeValue(ctx, resolving_funcs[0]);
  JS_FreeValue(ctx, resolving_funcs[1]);
  return promise;
}

// Formats a single JSValue for console/print output.
std::string FormatValueForPrint(JSContext* ctx, JSValueConst val) {
  if (JS_IsString(val)) {
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx, &len, val);
    if (cstr == nullptr) return "";
    std::string out(cstr, len);
    JS_FreeCString(ctx, cstr);
    return out;
  }
  if (JS_IsUndefined(val)) return "undefined";
  if (JS_IsNull(val)) return "null";
  if (JS_IsObject(val) && !JS_IsFunction(ctx, val)) {
    JSValue indent = JS_NewInt32(ctx, 2);
    JSValue json = JS_JSONStringify(ctx, val, JS_UNDEFINED, indent);
    JS_FreeValue(ctx, indent);
    if (!JS_IsException(json) && JS_IsString(json)) {
      const char* cstr = JS_ToCString(ctx, json);
      std::string out = (cstr != nullptr) ? cstr : "";
      if (cstr != nullptr) JS_FreeCString(ctx, cstr);
      JS_FreeValue(ctx, json);
      return out;
    }
    if (JS_IsException(json)) JS_FreeValue(ctx, JS_GetException(ctx));
    JS_FreeValue(ctx, json);
  }
  const char* cstr = JS_ToCString(ctx, val);
  if (cstr == nullptr) return "";
  std::string out(cstr);
  JS_FreeCString(ctx, cstr);
  return out;
}

// Computes visual display column width of a UTF-8 string (ignoring ANSI codes).
size_t DisplayWidth(std::string_view s) {
  size_t width = 0;
  size_t i = 0;
  while (i < s.size()) {
    unsigned char ch = static_cast<unsigned char>(s[i]);
    if (ch == 0x1b && i + 1 < s.size() && s[i + 1] == '[') {
      i += 2;
      while (i < s.size() &&
             (static_cast<unsigned char>(s[i]) < 0x40 ||
              static_cast<unsigned char>(s[i]) > 0x7e)) {
        ++i;
      }
      if (i < s.size()) ++i;
      continue;
    }
    if ((ch & 0x80) == 0) {
      ++width;
      ++i;
    } else if ((ch & 0xE0) == 0xC0) {
      ++width;
      i += 2;
    } else if ((ch & 0xF0) == 0xE0) {
      ++width;
      i += 3;
    } else {
      width += 2;
      i += 4;
    }
  }
  return width;
}

std::string PadCell(std::string_view text, size_t width) {
  size_t w = DisplayWidth(text);
  std::string out(text);
  if (w < width) out.append(width - w, ' ');
  return out;
}

void RepeatBoxHoriz(std::string& out, size_t count) {
  for (size_t i = 0; i < count; ++i) out += "─";
}

// Renders a JS array of objects or array of arrays as a Unicode box-drawn table.
void RenderTableToStdout(JSContext* ctx, JSValueConst data) {
  if (!JS_IsArray(ctx, data)) {
    std::string formatted = FormatValueForPrint(ctx, data);
    std::fwrite(formatted.data(), 1, formatted.size(), stdout);
    std::fwrite("\n", 1, 1, stdout);
    std::fflush(stdout);
    return;
  }

  JSValue len_val = JS_GetPropertyStr(ctx, data, "length");
  uint32_t row_count = 0;
  JS_ToUint32(ctx, &row_count, len_val);
  JS_FreeValue(ctx, len_val);
  if (row_count == 0) {
    std::fwrite("(empty table)\n", 1, 14, stdout);
    std::fflush(stdout);
    return;
  }

  std::vector<std::string> headers;
  std::unordered_map<std::string, size_t> header_index;
  std::vector<std::vector<std::string>> rows;
  rows.resize(row_count);

  for (uint32_t r = 0; r < row_count; ++r) {
    JSValue row_val = JS_GetPropertyUint32(ctx, data, r);
    if (JS_IsArray(ctx, row_val)) {
      JSValue rlen_val = JS_GetPropertyStr(ctx, row_val, "length");
      uint32_t rlen = 0;
      JS_ToUint32(ctx, &rlen, rlen_val);
      JS_FreeValue(ctx, rlen_val);
      for (uint32_t c = 0; c < rlen; ++c) {
        std::string col_name = std::to_string(c);
        auto it = header_index.find(col_name);
        size_t col_idx = 0;
        if (it == header_index.end()) {
          col_idx = headers.size();
          header_index[col_name] = col_idx;
          headers.push_back(col_name);
        } else {
          col_idx = it->second;
        }
        if (rows[r].size() <= col_idx) rows[r].resize(col_idx + 1);
        JSValue cell = JS_GetPropertyUint32(ctx, row_val, c);
        rows[r][col_idx] = FormatValueForPrint(ctx, cell);
        JS_FreeValue(ctx, cell);
      }
    } else if (JS_IsObject(row_val)) {
      JSPropertyEnum* tab = nullptr;
      uint32_t plen = 0;
      if (JS_GetOwnPropertyNames(ctx, &tab, &plen, row_val,
                                 JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t p = 0; p < plen; ++p) {
          const char* kstr = JS_AtomToCString(ctx, tab[p].atom);
          if (kstr == nullptr) continue;
          std::string col_name(kstr);
          JS_FreeCString(ctx, kstr);
          auto it = header_index.find(col_name);
          size_t col_idx = 0;
          if (it == header_index.end()) {
            col_idx = headers.size();
            header_index[col_name] = col_idx;
            headers.push_back(col_name);
          } else {
            col_idx = it->second;
          }
          if (rows[r].size() <= col_idx) rows[r].resize(col_idx + 1);
          JSValue cell = JS_GetProperty(ctx, row_val, tab[p].atom);
          rows[r][col_idx] = FormatValueForPrint(ctx, cell);
          JS_FreeValue(ctx, cell);
        }
        JS_FreePropertyEnum(ctx, tab, plen);
      }
    } else {
      if (headers.empty()) {
        header_index["value"] = 0;
        headers.push_back("value");
      }
      rows[r].resize(headers.size());
      rows[r][0] = FormatValueForPrint(ctx, row_val);
    }
    JS_FreeValue(ctx, row_val);
  }

  if (headers.empty()) return;

  size_t col_count = headers.size();
  std::vector<size_t> widths(col_count, 0);
  for (size_t c = 0; c < col_count; ++c)
    widths[c] = DisplayWidth(headers[c]);
  for (auto& row : rows) {
    row.resize(col_count);
    for (size_t c = 0; c < col_count; ++c)
      widths[c] = std::max(widths[c], DisplayWidth(row[c]));
  }

  std::string out;
  out += "┌";
  for (size_t c = 0; c < col_count; ++c) {
    if (c > 0) out += "┬";
    RepeatBoxHoriz(out, widths[c] + 2);
  }
  out += "┐\n│";
  for (size_t c = 0; c < col_count; ++c) {
    out += " \x1b[1m" + PadCell(headers[c], widths[c]) + "\x1b[0m │";
  }
  out += "\n├";
  for (size_t c = 0; c < col_count; ++c) {
    if (c > 0) out += "┼";
    RepeatBoxHoriz(out, widths[c] + 2);
  }
  out += "┤\n";
  for (const auto& row : rows) {
    out += "│";
    for (size_t c = 0; c < col_count; ++c) {
      out += " " + PadCell(row[c], widths[c]) + " │";
    }
    out += "\n";
  }
  out += "└";
  for (size_t c = 0; c < col_count; ++c) {
    if (c > 0) out += "┴";
    RepeatBoxHoriz(out, widths[c] + 2);
  }
  out += "┘\n";

  std::fwrite(out.data(), 1, out.size(), stdout);
  std::fflush(stdout);
}

// build memory usage object from QuickJS runtime.
JSValue BuildQuickJsMemoryObject(JSContext* ctx) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  JSMemoryUsage stats{};
  JS_ComputeMemoryUsage(rt, &stats);

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "mallocSize", JS_NewInt64(ctx, stats.malloc_size));
  JS_SetPropertyStr(ctx, obj, "mallocCount",
                    JS_NewInt64(ctx, stats.malloc_count));
  JS_SetPropertyStr(ctx, obj, "memoryUsedSize",
                    JS_NewInt64(ctx, stats.memory_used_size));
  JS_SetPropertyStr(ctx, obj, "memoryUsedCount",
                    JS_NewInt64(ctx, stats.memory_used_count));
  JS_SetPropertyStr(ctx, obj, "atomCount", JS_NewInt64(ctx, stats.atom_count));
  JS_SetPropertyStr(ctx, obj, "atomSize", JS_NewInt64(ctx, stats.atom_size));
  JS_SetPropertyStr(ctx, obj, "strCount", JS_NewInt64(ctx, stats.str_count));
  JS_SetPropertyStr(ctx, obj, "strSize", JS_NewInt64(ctx, stats.str_size));
  JS_SetPropertyStr(ctx, obj, "objCount", JS_NewInt64(ctx, stats.obj_count));
  JS_SetPropertyStr(ctx, obj, "objSize", JS_NewInt64(ctx, stats.obj_size));
  JS_SetPropertyStr(ctx, obj, "propCount", JS_NewInt64(ctx, stats.prop_count));
  JS_SetPropertyStr(ctx, obj, "propSize", JS_NewInt64(ctx, stats.prop_size));
  JS_SetPropertyStr(ctx, obj, "shapeCount",
                    JS_NewInt64(ctx, stats.shape_count));
  JS_SetPropertyStr(ctx, obj, "shapeSize", JS_NewInt64(ctx, stats.shape_size));
  JS_SetPropertyStr(ctx, obj, "jsFuncCount",
                    JS_NewInt64(ctx, stats.js_func_count));
  JS_SetPropertyStr(ctx, obj, "jsFuncSize",
                    JS_NewInt64(ctx, stats.js_func_size));
  JS_SetPropertyStr(ctx, obj, "cFuncCount",
                    JS_NewInt64(ctx, stats.c_func_count));
  JS_SetPropertyStr(ctx, obj, "arrayCount",
                    JS_NewInt64(ctx, stats.array_count));
  return obj;
}

// ---- sys.* functions ----

JSValue JsSysMemory(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                    JSValueConst* /*argv*/) {
  size_t total_mem = 0;
  size_t shared_mem = 0;
  size_t free_mem = 0;
  size_t cores = 0;
  perception::GetSystemMetrics(total_mem, shared_mem, free_mem, cores);
  size_t used_mem = (total_mem > free_mem) ? (total_mem - free_mem) : 0;

  size_t proc_unique = 0;
  size_t proc_shared = 0;
  size_t proc_created = 0;
  size_t proc_services = 0;
  uint8 cpu_pcts[kMaxCpuCoreMetrics] = {};
  perception::GetProcessHealthMetrics(0, proc_unique, proc_shared, proc_created,
                                      proc_services, cpu_pcts,
                                      kMaxCpuCoreMetrics);

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "totalBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(total_mem)));
  JS_SetPropertyStr(ctx, obj, "freeBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(free_mem)));
  JS_SetPropertyStr(ctx, obj, "usedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(used_mem)));
  JS_SetPropertyStr(ctx, obj, "sharedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(shared_mem)));

  JSValue sys_sub = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, sys_sub, "totalBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(total_mem)));
  JS_SetPropertyStr(ctx, sys_sub, "freeBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(free_mem)));
  JS_SetPropertyStr(ctx, sys_sub, "usedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(used_mem)));
  JS_SetPropertyStr(ctx, sys_sub, "sharedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(shared_mem)));
  JS_SetPropertyStr(ctx, obj, "system", sys_sub);

  JSValue proc_sub = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, proc_sub, "uniqueBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(proc_unique)));
  JS_SetPropertyStr(ctx, proc_sub, "sharedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(proc_shared)));
  JS_SetPropertyStr(
      ctx, proc_sub, "totalBytes",
      JS_NewInt64(ctx, static_cast<int64_t>(proc_unique + proc_shared)));
  JS_SetPropertyStr(ctx, obj, "process", proc_sub);

  JS_SetPropertyStr(ctx, obj, "quickjs", BuildQuickJsMemoryObject(ctx));
  return obj;
}

JSValue JsSysCores(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                   JSValueConst* /*argv*/) {
  return JS_NewUint32(ctx,
                      static_cast<uint32_t>(perception::GetSystemCoreCount()));
}

JSValue JsSysUptime(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                    JSValueConst* /*argv*/) {
  auto us = perception::GetTimeSinceKernelStarted();
  return JS_NewFloat64(
      ctx, static_cast<double>(us.count()) / kMicrosecondsPerMillisecond);
}

JSValue JsSysSleep(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                   JSValueConst* argv) {
  int64_t ms = 0;
  if (argc >= 1) JS_ToInt64(ctx, &ms, argv[0]);
  if (ms > 0)
    perception::SleepForDuration(std::chrono::milliseconds(ms));
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysExit(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                  JSValueConst* argv) {
  int32_t code = 0;
  if (argc >= 1) JS_ToInt32(ctx, &code, argv[0]);
  JsEngine* engine = GetEngine(ctx);
  if (engine != nullptr) engine->RestoreTerminalState();
  std::exit(code);
  return JS_UNDEFINED;
}

JSValue JsSysGc(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                JSValueConst* /*argv*/) {
  JSRuntime* rt = JS_GetRuntime(ctx);
  if (rt != nullptr) JS_RunGC(rt);
  return BuildQuickJsMemoryObject(ctx);
}

JSValue JsSysProfile(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                     JSValueConst* argv) {
  perception::ProcessId pid = 0;
  if (argc >= 1 && JS_IsNumber(argv[0])) {
    uint32_t p = 0;
    JS_ToUint32(ctx, &p, argv[0]);
    pid = static_cast<perception::ProcessId>(p);
  }
  size_t unique_bytes = 0;
  size_t shared_bytes = 0;
  size_t creation_us = 0;
  size_t reg_services = 0;
  uint8 cpu_pcts[kMaxCpuCoreMetrics] = {};
  size_t core_count =
      std::min(perception::GetSystemCoreCount(), kMaxCpuCoreMetrics);
  perception::GetProcessHealthMetrics(pid, unique_bytes, shared_bytes,
                                      creation_us, reg_services, cpu_pcts,
                                      core_count);

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(
      ctx, obj, "pid",
      JS_NewUint32(ctx, static_cast<uint32_t>(
                            pid == 0 ? perception::GetProcessId() : pid)));
  JS_SetPropertyStr(
      ctx, obj, "allocatedMemoryPages",
      JS_NewInt64(ctx, static_cast<int64_t>(
                           (unique_bytes + shared_bytes) / perception::kPageSize)));
  JS_SetPropertyStr(ctx, obj, "uniqueBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(unique_bytes)));
  JS_SetPropertyStr(ctx, obj, "sharedBytes",
                    JS_NewInt64(ctx, static_cast<int64_t>(shared_bytes)));
  JS_SetPropertyStr(ctx, obj, "creationTimeUs",
                    JS_NewInt64(ctx, static_cast<int64_t>(creation_us)));
  JS_SetPropertyStr(ctx, obj, "registeredServices",
                    JS_NewInt64(ctx, static_cast<int64_t>(reg_services)));

  JSValue cpu_arr = JS_NewArray(ctx);
  for (size_t i = 0; i < core_count; ++i) {
    JS_SetPropertyUint32(ctx, cpu_arr, static_cast<uint32_t>(i),
                         JS_NewUint32(ctx, cpu_pcts[i]));
  }
  JS_SetPropertyStr(ctx, obj, "cpuPercentages", cpu_arr);
  return obj;
}

JSValue JsSysMounts(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                    JSValueConst* /*argv*/) {
  auto status_or =
      perception::GetService<perception::StorageManager>().GetMountedFileSystems();
  if (!status_or.Ok()) {
    return JS_ThrowInternalError(ctx, "Failed to query mounted filesystems");
  }
  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;
  for (const auto& fs : status_or->file_systems) {
    JSValue item = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, item, "path",
                      JS_NewString(ctx, fs.mount_point.c_str()));
    JS_SetPropertyStr(ctx, item, "mountPoint",
                      JS_NewString(ctx, fs.mount_point.c_str()));
    JS_SetPropertyStr(ctx, item, "type",
                      JS_NewString(ctx, fs.filesystem_type.c_str()));
    JS_SetPropertyStr(ctx, item, "filesystemType",
                      JS_NewString(ctx, fs.filesystem_type.c_str()));
    JS_SetPropertyStr(ctx, item, "readOnly",
                      JS_NewBool(ctx, fs.is_writable ? 0 : 1));
    JS_SetPropertyStr(ctx, item, "writable",
                      JS_NewBool(ctx, fs.is_writable ? 1 : 0));
    JS_SetPropertyStr(ctx, item, "bootDrive",
                      JS_NewBool(ctx, fs.is_boot_drive ? 1 : 0));
    JS_SetPropertyStr(ctx, item, "deviceName",
                      JS_NewString(ctx, fs.device_name.c_str()));
    JS_SetPropertyStr(ctx, item, "startByteOffset",
                      JS_NewInt64(ctx, static_cast<int64_t>(fs.start_byte_offset)));
    JS_SetPropertyStr(ctx, item, "byteLength",
                      JS_NewInt64(ctx, static_cast<int64_t>(fs.byte_length)));
    JS_SetPropertyUint32(ctx, arr, idx++, item);
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsSysMount(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                   JSValueConst* argv) {
  if (argc < 2) {
    return JS_ThrowTypeError(
        ctx, "sys.mount requires (deviceName, mountPoint) arguments");
  }
  const char* a0 = JS_ToCString(ctx, argv[0]);
  const char* a1 = JS_ToCString(ctx, argv[1]);
  if (a0 == nullptr || a1 == nullptr) {
    if (a0 != nullptr) JS_FreeCString(ctx, a0);
    if (a1 != nullptr) JS_FreeCString(ctx, a1);
    return JS_EXCEPTION;
  }
  std::string s0(a0);
  std::string s1(a1);
  JS_FreeCString(ctx, a0);
  JS_FreeCString(ctx, a1);

  // Support both (deviceName, mountPoint) and (mountPoint, deviceName).
  std::string dev_name = s0;
  std::string mount_point = s1;
  if (!s0.empty() && s0[0] == '/' && (s1.empty() || s1[0] != '/')) {
    mount_point = s0;
    dev_name = s1;
  }

  uint64_t offset = 0;
  uint64_t length = 0;
  if (argc >= 3 && JS_IsObject(argv[2])) {
    JSValue off_val = JS_GetPropertyStr(ctx, argv[2], "offset");
    if (JS_IsNumber(off_val)) {
      int64_t v = 0;
      JS_ToInt64(ctx, &v, off_val);
      if (v > 0) offset = static_cast<uint64_t>(v);
    }
    JS_FreeValue(ctx, off_val);

    JSValue len_val = JS_GetPropertyStr(ctx, argv[2], "length");
    if (JS_IsNumber(len_val)) {
      int64_t v = 0;
      JS_ToInt64(ctx, &v, len_val);
      if (v > 0) length = static_cast<uint64_t>(v);
    }
    JS_FreeValue(ctx, len_val);
  }

  std::optional<perception::devices::StorageDevice::Client> matched_device;
  perception::ForEachInstanceOfService<perception::devices::StorageDevice>(
      [&](perception::devices::StorageDevice::Client client) {
        if (matched_device.has_value()) return;
        auto details = client.GetDeviceDetails();
        if (details.Ok() && details->name == dev_name)
          matched_device = client;
      });

  if (!matched_device.has_value()) {
    return JS_ThrowInternalError(ctx, "Storage device not found: %s",
                                 dev_name.c_str());
  }

  perception::MountFileSystemRequest req;
  req.device = *matched_device;
  req.start_byte_offset = offset;
  req.byte_length = length;
  req.target_mount_point = mount_point;

  auto resp = perception::GetService<perception::StorageManager>().MountFileSystem(req);
  if (!resp.Ok()) {
    return JS_ThrowInternalError(ctx, "Failed to mount %s at %s",
                                 dev_name.c_str(), mount_point.c_str());
  }
  return MakeResolvedPromise(ctx,
                             JS_NewString(ctx, resp->mount_point.c_str()));
}

JSValue JsSysUnmount(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                     JSValueConst* argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "sys.unmount requires a mountPoint argument");
  const char* path_cstr = JS_ToCString(ctx, argv[0]);
  if (path_cstr == nullptr) return JS_EXCEPTION;
  perception::RequestWithFilePath req(path_cstr);
  JS_FreeCString(ctx, path_cstr);

  auto status = perception::GetService<perception::StorageManager>().UnmountFileSystem(req);
  if (status != Status::OK)
    return JS_ThrowInternalError(ctx, "Failed to unmount %s", req.path.c_str());
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysSetMountPath(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                          JSValueConst* argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(
        ctx, "sys.setMountPath requires (newPath) or (oldPath, newPath)");
  }
  perception::SetMountPathRequest req;
  if (argc == 1) {
    const char* p = JS_ToCString(ctx, argv[0]);
    if (p == nullptr) return JS_EXCEPTION;
    req.old_mount_point = "/";
    req.new_mount_point = p;
    JS_FreeCString(ctx, p);
  } else {
    const char* old_p = JS_ToCString(ctx, argv[0]);
    const char* new_p = JS_ToCString(ctx, argv[1]);
    if (old_p == nullptr || new_p == nullptr) {
      if (old_p != nullptr) JS_FreeCString(ctx, old_p);
      if (new_p != nullptr) JS_FreeCString(ctx, new_p);
      return JS_EXCEPTION;
    }
    req.old_mount_point = old_p;
    req.new_mount_point = new_p;
    JS_FreeCString(ctx, old_p);
    JS_FreeCString(ctx, new_p);
  }

  auto status = perception::GetService<perception::StorageManager>().SetMountPath(req);
  if (status != Status::OK)
    return JS_ThrowInternalError(ctx, "Failed to set mount path");
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysPowerOff(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                      JSValueConst* /*argv*/) {
  perception::power::PowerOff();
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysRestart(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                     JSValueConst* /*argv*/) {
  perception::power::Restart();
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysSuspend(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                     JSValueConst* /*argv*/) {
  perception::power::Sleep();
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsSysWake(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                  JSValueConst* /*argv*/) {
  perception::power::Wake();
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

// ---- registry.* functions ----

JSValue JsRegistryNamespaces(JSContext* ctx, JSValueConst /*this_val*/,
                             int argc, JSValueConst* argv) {
  bool filter_corpus = false;
  perception::RegistryCorpus target_corpus =
      perception::RegistryCorpus::APPLICATIONS;
  if (argc >= 1 && JS_IsString(argv[0])) {
    const char* cstr = JS_ToCString(ctx, argv[0]);
    if (cstr != nullptr) {
      filter_corpus = true;
      target_corpus = ParseCorpus(cstr);
      JS_FreeCString(ctx, cstr);
    }
  }

  auto status_or = perception::GetNamespacesInRegistry();
  if (!status_or.Ok())
    return JS_ThrowInternalError(ctx, "Failed to query registry namespaces");

  JSValue arr = JS_NewArray(ctx);
  uint32_t idx = 0;
  for (const auto& ns : *status_or) {
    if (filter_corpus && ns.corpus != target_corpus) continue;
    JSValue item = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, item, "corpus",
                      JS_NewString(ctx, CorpusToString(ns.corpus).c_str()));
    JS_SetPropertyStr(ctx, item, "name", JS_NewString(ctx, ns.name.c_str()));
    JS_SetPropertyUint32(ctx, arr, idx++, item);
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsRegistryKeys(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                       JSValueConst* argv) {
  StatusOr<std::vector<std::string>> status_or(Status::INVALID_ARGUMENT);
  if (argc == 0) {
    status_or = perception::GetRegistryKeys();
  } else if (argc == 1) {
    const char* ns = JS_ToCString(ctx, argv[0]);
    if (ns == nullptr) return JS_EXCEPTION;
    status_or = perception::GetRegistryKeys(
        perception::RegistryCorpus::APPLICATIONS, ns);
    JS_FreeCString(ctx, ns);
  } else {
    const char* a0 = JS_ToCString(ctx, argv[0]);
    const char* a1 = JS_ToCString(ctx, argv[1]);
    if (a0 == nullptr || a1 == nullptr) {
      if (a0 != nullptr) JS_FreeCString(ctx, a0);
      if (a1 != nullptr) JS_FreeCString(ctx, a1);
      return JS_EXCEPTION;
    }
    std::string s0(a0);
    std::string s1(a1);
    JS_FreeCString(ctx, a0);
    JS_FreeCString(ctx, a1);

    if (IsCorpusName(s0)) {
      status_or = perception::GetRegistryKeys(ParseCorpus(s0), s1);
    } else {
      status_or = perception::GetRegistryKeys(ParseCorpus(s1), s0);
    }
  }

  if (!status_or.Ok())
    return JS_ThrowInternalError(ctx, "Failed to query registry keys");

  JSValue arr = JS_NewArray(ctx);
  for (size_t i = 0; i < status_or->size(); ++i) {
    JS_SetPropertyUint32(ctx, arr, static_cast<uint32_t>(i),
                         JS_NewString(ctx, (*status_or)[i].c_str()));
  }
  return MakeResolvedPromise(ctx, arr);
}

JSValue JsRegistryGet(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "registry.get requires at least 1 argument");

  StatusOr<perception::serialization::Value> status_or(
      Status::INVALID_ARGUMENT);
  if (argc == 1) {
    const char* k = JS_ToCString(ctx, argv[0]);
    if (k == nullptr) return JS_EXCEPTION;
    status_or = perception::GetRegistryValue(k);
    JS_FreeCString(ctx, k);
  } else if (argc == 2) {
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* k = JS_ToCString(ctx, argv[1]);
    if (ns == nullptr || k == nullptr) {
      if (ns != nullptr) JS_FreeCString(ctx, ns);
      if (k != nullptr) JS_FreeCString(ctx, k);
      return JS_EXCEPTION;
    }
    status_or = perception::GetRegistryValue(
        perception::RegistryCorpus::APPLICATIONS, ns, k);
    JS_FreeCString(ctx, ns);
    JS_FreeCString(ctx, k);
  } else {
    const char* a0 = JS_ToCString(ctx, argv[0]);
    const char* a1 = JS_ToCString(ctx, argv[1]);
    const char* a2 = JS_ToCString(ctx, argv[2]);
    if (a0 == nullptr || a1 == nullptr || a2 == nullptr) {
      if (a0 != nullptr) JS_FreeCString(ctx, a0);
      if (a1 != nullptr) JS_FreeCString(ctx, a1);
      if (a2 != nullptr) JS_FreeCString(ctx, a2);
      return JS_EXCEPTION;
    }
    std::string s0(a0);
    std::string s1(a1);
    std::string s2(a2);
    JS_FreeCString(ctx, a0);
    JS_FreeCString(ctx, a1);
    JS_FreeCString(ctx, a2);

    if (IsCorpusName(s0)) {
      status_or = perception::GetRegistryValue(ParseCorpus(s0), s1, s2);
    } else {
      status_or = perception::GetRegistryValue(ParseCorpus(s2), s0, s1);
    }
  }

  if (!status_or.Ok()) return MakeResolvedPromise(ctx, JS_UNDEFINED);
  return MakeResolvedPromise(ctx, SerializationValueToJs(ctx, *status_or));
}

JSValue JsRegistrySet(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  if (argc < 2)
    return JS_ThrowTypeError(ctx, "registry.set requires at least 2 arguments");

  if (argc == 2) {
    const char* k = JS_ToCString(ctx, argv[0]);
    if (k == nullptr) return JS_EXCEPTION;
    perception::SetRegistryValue(k, JsToSerializationValue(ctx, argv[1]));
    JS_FreeCString(ctx, k);
    return MakeResolvedPromise(ctx, JS_UNDEFINED);
  }
  if (argc == 3) {
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* k = JS_ToCString(ctx, argv[1]);
    if (ns == nullptr || k == nullptr) {
      if (ns != nullptr) JS_FreeCString(ctx, ns);
      if (k != nullptr) JS_FreeCString(ctx, k);
      return JS_EXCEPTION;
    }
    perception::SetRegistryValue(perception::RegistryCorpus::APPLICATIONS, ns,
                                 k, JsToSerializationValue(ctx, argv[2]));
    JS_FreeCString(ctx, ns);
    JS_FreeCString(ctx, k);
    return MakeResolvedPromise(ctx, JS_UNDEFINED);
  }

  const char* a0 = JS_ToCString(ctx, argv[0]);
  const char* a1 = JS_ToCString(ctx, argv[1]);
  if (a0 == nullptr || a1 == nullptr) {
    if (a0 != nullptr) JS_FreeCString(ctx, a0);
    if (a1 != nullptr) JS_FreeCString(ctx, a1);
    return JS_EXCEPTION;
  }
  std::string s0(a0);
  std::string s1(a1);
  JS_FreeCString(ctx, a0);
  JS_FreeCString(ctx, a1);

  if (IsCorpusName(s0)) {
    const char* a2 = JS_ToCString(ctx, argv[2]);
    if (a2 == nullptr) return JS_EXCEPTION;
    std::string s2(a2);
    JS_FreeCString(ctx, a2);
    perception::SetRegistryValue(ParseCorpus(s0), s1, s2,
                                 JsToSerializationValue(ctx, argv[3]));
  } else {
    const char* a3 = JS_ToCString(ctx, argv[3]);
    std::string s3 = (a3 != nullptr) ? a3 : "applications";
    if (a3 != nullptr) JS_FreeCString(ctx, a3);
    perception::SetRegistryValue(ParseCorpus(s3), s0, s1,
                                 JsToSerializationValue(ctx, argv[2]));
  }
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsRegistryDelete(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                         JSValueConst* argv) {
  if (argc < 1) {
    return JS_ThrowTypeError(ctx,
                             "registry.delete requires at least 1 argument");
  }
  if (argc == 1) {
    const char* k = JS_ToCString(ctx, argv[0]);
    if (k == nullptr) return JS_EXCEPTION;
    perception::DeleteRegistryValue(k);
    JS_FreeCString(ctx, k);
  } else if (argc == 2) {
    const char* ns = JS_ToCString(ctx, argv[0]);
    const char* k = JS_ToCString(ctx, argv[1]);
    if (ns == nullptr || k == nullptr) {
      if (ns != nullptr) JS_FreeCString(ctx, ns);
      if (k != nullptr) JS_FreeCString(ctx, k);
      return JS_EXCEPTION;
    }
    perception::DeleteRegistryValue(perception::RegistryCorpus::APPLICATIONS,
                                    ns, k);
    JS_FreeCString(ctx, ns);
    JS_FreeCString(ctx, k);
  } else {
    const char* a0 = JS_ToCString(ctx, argv[0]);
    const char* a1 = JS_ToCString(ctx, argv[1]);
    const char* a2 = JS_ToCString(ctx, argv[2]);
    if (a0 == nullptr || a1 == nullptr || a2 == nullptr) {
      if (a0 != nullptr) JS_FreeCString(ctx, a0);
      if (a1 != nullptr) JS_FreeCString(ctx, a1);
      if (a2 != nullptr) JS_FreeCString(ctx, a2);
      return JS_EXCEPTION;
    }
    std::string s0(a0);
    std::string s1(a1);
    std::string s2(a2);
    JS_FreeCString(ctx, a0);
    JS_FreeCString(ctx, a1);
    JS_FreeCString(ctx, a2);
    if (IsCorpusName(s0)) {
      perception::DeleteRegistryValue(ParseCorpus(s0), s1, s2);
    } else {
      perception::DeleteRegistryValue(ParseCorpus(s2), s0, s1);
    }
  }
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

JSValue JsRegistryFlush(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                        JSValueConst* /*argv*/) {
  auto status = perception::FlushRegistry();
  if (status != Status::OK)
    return JS_ThrowInternalError(ctx, "Failed to flush registry");
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

// ---- clipboard.* functions ----

JSValue JsClipboardGet(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                       JSValueConst* /*argv*/) {
  auto status_or = perception::GetClipboard();
  if (!status_or.Ok()) return MakeResolvedPromise(ctx, JS_UNDEFINED);
  return MakeResolvedPromise(ctx, SerializationValueToJs(ctx, *status_or));
}

JSValue JsClipboardSet(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                       JSValueConst* argv) {
  if (argc < 1)
    return JS_ThrowTypeError(ctx, "clipboard.set requires a value argument");
  perception::SetClipboard(JsToSerializationValue(ctx, argv[0]));
  return MakeResolvedPromise(ctx, JS_UNDEFINED);
}

// ---- term.* functions ----

JSValue JsTermClear(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                    JSValueConst* /*argv*/) {
  JsEngine* engine = GetEngine(ctx);
  if (engine != nullptr) engine->ClearVisibleHistory();
  std::fwrite(kAnsiClearScreen.data(), 1, kAnsiClearScreen.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermTitle(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                    JSValueConst* argv) {
  if (argc < 1) return JS_UNDEFINED;
  const char* title = JS_ToCString(ctx, argv[0]);
  if (title == nullptr) return JS_EXCEPTION;
  std::string seq = std::string("\x1b]0;") + title + "\x07";
  JS_FreeCString(ctx, title);
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermWrite(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                    JSValueConst* argv) {
  for (int i = 0; i < argc; ++i) {
    std::string text = FormatValueForPrint(ctx, argv[i]);
    if (!text.empty()) std::fwrite(text.data(), 1, text.size(), stdout);
  }
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermSize(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                   JSValueConst* /*argv*/) {
  int cols = kDefaultTermCols;
  int rows = kDefaultTermRows;
  int width_px = 0;
  int height_px = 0;

  struct winsize ws {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 ||
      ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 ||
      tcgetwinsize(STDIN_FILENO, &ws) == 0) {
    if (ws.ws_col > 0) cols = ws.ws_col;
    if (ws.ws_row > 0) rows = ws.ws_row;
    width_px = ws.ws_xpixel;
    height_px = ws.ws_ypixel;
  }

  JSValue obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, obj, "cols", JS_NewInt32(ctx, cols));
  JS_SetPropertyStr(ctx, obj, "rows", JS_NewInt32(ctx, rows));
  JS_SetPropertyStr(ctx, obj, "widthPx", JS_NewInt32(ctx, width_px));
  JS_SetPropertyStr(ctx, obj, "heightPx", JS_NewInt32(ctx, height_px));
  return obj;
}

JSValue JsTermMoveTo(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                     JSValueConst* argv) {
  int32_t row = 1;
  int32_t col = 1;
  if (argc >= 1) JS_ToInt32(ctx, &row, argv[0]);
  if (argc >= 2) JS_ToInt32(ctx, &col, argv[1]);
  row = std::max<int32_t>(1, row);
  col = std::max<int32_t>(1, col);
  char buf[48];
  int len = std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
  if (len > 0) {
    std::fwrite(buf, 1, static_cast<size_t>(len), stdout);
    std::fflush(stdout);
  }
  return JS_UNDEFINED;
}

JSValue JsTermCursor(JSContext* ctx, JSValueConst this_val, int argc,
                     JSValueConst* argv) {
  if (argc >= 2 || (argc == 1 && JS_IsNumber(argv[0])))
    return JsTermMoveTo(ctx, this_val, argc, argv);
  bool visible = true;
  if (argc >= 1) visible = (JS_ToBool(ctx, argv[0]) != 0);
  if (JsEngine* engine = GetEngine(ctx))
    engine->SetScriptCursorVisible(visible);
  std::string_view seq = visible ? kAnsiShowCursor : kAnsiHideCursor;
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermHideCursor(JSContext* ctx, JSValueConst /*this_val*/,
                         int /*argc*/, JSValueConst* /*argv*/) {
  if (JsEngine* engine = GetEngine(ctx))
    engine->SetScriptCursorVisible(false);
  std::fwrite(kAnsiHideCursor.data(), 1, kAnsiHideCursor.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermShowCursor(JSContext* ctx, JSValueConst /*this_val*/,
                         int /*argc*/, JSValueConst* /*argv*/) {
  if (JsEngine* engine = GetEngine(ctx))
    engine->SetScriptCursorVisible(true);
  std::fwrite(kAnsiShowCursor.data(), 1, kAnsiShowCursor.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermAltScreen(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                        JSValueConst* argv) {
  bool enable = true;
  if (argc >= 1) enable = (JS_ToBool(ctx, argv[0]) != 0);
  if (JsEngine* engine = GetEngine(ctx))
    engine->SetScriptAltScreen(enable);
  std::string_view seq = enable ? kAnsiAltScreenEnter : kAnsiAltScreenLeave;
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermRawMode(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  bool enable = true;
  if (argc >= 1) enable = (JS_ToBool(ctx, argv[0]) != 0);
  if (JsEngine* engine = GetEngine(ctx))
    engine->SetScriptRawMode(enable);
  struct termios t {};
  if (tcgetattr(STDIN_FILENO, &t) == 0) {
    if (enable) {
      t.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    } else {
      t.c_lflag |= static_cast<tcflag_t>(ICANON | ECHO | ISIG);
    }
    tcsetattr(STDIN_FILENO, TCSANOW, &t);
  }
  std::string_view seq = enable ? kAnsiKittyKbdEnter : kAnsiKittyKbdLeave;
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermMouse(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                    JSValueConst* argv) {
  bool enable = true;
  if (argc >= 1) enable = (JS_ToBool(ctx, argv[0]) != 0);
  std::string_view seq = enable ? kAnsiMouseEnter : kAnsiMouseLeave;
  std::fwrite(seq.data(), 1, seq.size(), stdout);
  std::fflush(stdout);
  return JS_UNDEFINED;
}

JSValue JsTermReadKey(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  int32_t timeout_ms = -1;
  if (argc >= 1 && JS_IsNumber(argv[0]))
    JS_ToInt32(ctx, &timeout_ms, argv[0]);
  std::string key = ReadTerminalKey(timeout_ms);
  if (key.empty()) return JS_NULL;
  return JS_NewStringLen(ctx, key.data(), key.size());
}

JSValue JsTermTable(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                    JSValueConst* argv) {
  if (argc >= 1) RenderTableToStdout(ctx, argv[0]);
  return JS_UNDEFINED;
}

JSValue JsTermInspect(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  JsEngine* engine = GetEngine(ctx);
  if (engine == nullptr) return JS_UNDEFINED;
  JSValueConst target = (argc >= 1) ? argv[0] : JS_UNDEFINED;
  std::string label = "value";
  if (argc >= 2 && JS_IsString(argv[1])) {
    const char* l = JS_ToCString(ctx, argv[1]);
    if (l != nullptr) {
      label = l;
      JS_FreeCString(ctx, l);
    }
  }
  RunObjectInspector(*engine, target, label);
  return JS_UNDEFINED;
}

JSValue JsTermMemory(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/,
                     JSValueConst* /*argv*/) {
  JsEngine* engine = GetEngine(ctx);
  if (engine != nullptr) RunMemoryExplorer(*engine);
  return JS_UNDEFINED;
}

// ---- term.style.* functions ----

JSValue WrapAnsiStyle(JSContext* ctx, int argc, JSValueConst* argv,
                      std::string_view prefix) {
  std::string text;
  if (argc >= 1) text = FormatValueForPrint(ctx, argv[0]);
  std::string out;
  out.reserve(prefix.size() + text.size() + kAnsiReset.size());
  out.append(prefix);
  out.append(text);
  out.append(kAnsiReset);
  return JS_NewStringLen(ctx, out.data(), out.size());
}

#define DEFINE_STYLE_FN(FnName, AnsiCode)                                      \
  JSValue FnName(JSContext* ctx, JSValueConst /*this_val*/, int argc,          \
                 JSValueConst* argv) {                                         \
    return WrapAnsiStyle(ctx, argc, argv, AnsiCode);                           \
  }

DEFINE_STYLE_FN(JsStyleBold, "\x1b[1m")
DEFINE_STYLE_FN(JsStyleDim, "\x1b[2m")
DEFINE_STYLE_FN(JsStyleItalic, "\x1b[3m")
DEFINE_STYLE_FN(JsStyleUnderline, "\x1b[4m")
DEFINE_STYLE_FN(JsStyleRed, "\x1b[31m")
DEFINE_STYLE_FN(JsStyleGreen, "\x1b[32m")
DEFINE_STYLE_FN(JsStyleYellow, "\x1b[33m")
DEFINE_STYLE_FN(JsStyleBlue, "\x1b[34m")
DEFINE_STYLE_FN(JsStyleMagenta, "\x1b[35m")
DEFINE_STYLE_FN(JsStyleCyan, "\x1b[36m")
DEFINE_STYLE_FN(JsStyleWhite, "\x1b[37m")
DEFINE_STYLE_FN(JsStyleGray, "\x1b[90m")
DEFINE_STYLE_FN(JsStyleBgRed, "\x1b[41m")
DEFINE_STYLE_FN(JsStyleBgGreen, "\x1b[42m")
DEFINE_STYLE_FN(JsStyleBgYellow, "\x1b[43m")
DEFINE_STYLE_FN(JsStyleBgBlue, "\x1b[44m")

#undef DEFINE_STYLE_FN

JSValue JsStyleRgb(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                   JSValueConst* argv) {
  int32_t r = 255;
  int32_t g = 255;
  int32_t b = 255;
  if (argc >= 1) JS_ToInt32(ctx, &r, argv[0]);
  if (argc >= 2) JS_ToInt32(ctx, &g, argv[1]);
  if (argc >= 3) JS_ToInt32(ctx, &b, argv[2]);
  r = std::clamp<int32_t>(r, 0, 255);
  g = std::clamp<int32_t>(g, 0, 255);
  b = std::clamp<int32_t>(b, 0, 255);
  std::string text = (argc >= 4) ? FormatValueForPrint(ctx, argv[3]) : "";
  char prefix[32];
  std::snprintf(prefix, sizeof(prefix), "\x1b[38;2;%d;%d;%dm", r, g, b);
  std::string out = std::string(prefix) + text + std::string(kAnsiReset);
  return JS_NewStringLen(ctx, out.data(), out.size());
}

JSValue JsStyleBgRgb(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                     JSValueConst* argv) {
  int32_t r = 0;
  int32_t g = 0;
  int32_t b = 0;
  if (argc >= 1) JS_ToInt32(ctx, &r, argv[0]);
  if (argc >= 2) JS_ToInt32(ctx, &g, argv[1]);
  if (argc >= 3) JS_ToInt32(ctx, &b, argv[2]);
  r = std::clamp<int32_t>(r, 0, 255);
  g = std::clamp<int32_t>(g, 0, 255);
  b = std::clamp<int32_t>(b, 0, 255);
  std::string text = (argc >= 4) ? FormatValueForPrint(ctx, argv[3]) : "";
  char prefix[32];
  std::snprintf(prefix, sizeof(prefix), "\x1b[48;2;%d;%d;%dm", r, g, b);
  std::string out = std::string(prefix) + text + std::string(kAnsiReset);
  return JS_NewStringLen(ctx, out.data(), out.size());
}

// ---- print and console.* functions ----

JSValue WriteFormattedArgs(JSContext* ctx, int argc, JSValueConst* argv,
                           FILE* stream, std::string_view color_prefix) {
  std::string out;
  if (!color_prefix.empty()) out.append(color_prefix);
  for (int i = 0; i < argc; ++i) {
    if (i > 0) out.push_back(' ');
    out.append(FormatValueForPrint(ctx, argv[i]));
  }
  if (!color_prefix.empty()) out.append(kAnsiReset);
  out.push_back('\n');
  std::fwrite(out.data(), 1, out.size(), stream);
  std::fflush(stream);
  return JS_UNDEFINED;
}

JSValue JsGlobalPrint(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  return WriteFormattedArgs(ctx, argc, argv, stdout, "");
}

JSValue JsConsoleWarn(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  return WriteFormattedArgs(ctx, argc, argv, stderr, "\x1b[33m");
}

JSValue JsConsoleError(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                       JSValueConst* argv) {
  return WriteFormattedArgs(ctx, argc, argv, stderr, "\x1b[31m");
}

JSValue JsConsoleTime(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                      JSValueConst* argv) {
  std::string label = "default";
  if (argc >= 1 && !JS_IsUndefined(argv[0]))
    label = FormatValueForPrint(ctx, argv[0]);
  ConsoleTimers()[label] = perception::GetTimeSinceKernelStarted();
  return JS_UNDEFINED;
}

JSValue JsConsoleTimeEnd(JSContext* ctx, JSValueConst /*this_val*/, int argc,
                         JSValueConst* argv) {
  std::string label = "default";
  if (argc >= 1 && !JS_IsUndefined(argv[0]))
    label = FormatValueForPrint(ctx, argv[0]);
  auto& timers = ConsoleTimers();
  auto it = timers.find(label);
  if (it == timers.end()) return JS_UNDEFINED;
  auto elapsed_us = perception::GetTimeSinceKernelStarted() - it->second;
  timers.erase(it);
  double ms =
      static_cast<double>(elapsed_us.count()) / kMicrosecondsPerMillisecond;
  char buf[128];
  int len = std::snprintf(buf, sizeof(buf), "%s: %.3f ms\n", label.c_str(), ms);
  if (len > 0) {
    std::fwrite(buf, 1, static_cast<size_t>(len), stdout);
    std::fflush(stdout);
  }
  return JS_UNDEFINED;
}

// Decodes a single key token from `PendingKeyBytes()`.
std::string PopDecodedKeyFromQueue() {
  auto& q = PendingKeyBytes();
  if (q.empty()) return "";

  unsigned char ch = q.front();
  q.pop_front();

  if (ch == 0x03) return "Ctrl+C";
  if (ch == '\r' || ch == '\n') return "Enter";
  if (ch == '\t') return "Tab";
  if (ch == 0x7f || ch == '\b') return "Backspace";

  if (ch == 0x1b) {
    if (q.empty()) return "Escape";
    unsigned char next = q.front();
    if (next == '[' || next == 'O') {
      q.pop_front();
      std::string body;
      char final_ch = 0;
      while (!q.empty()) {
        unsigned char c = q.front();
        q.pop_front();
        if (c >= 0x40 && c <= 0x7e) {
          final_ch = static_cast<char>(c);
          break;
        }
        body.push_back(static_cast<char>(c));
      }
      if (final_ch == 0) return "Escape";

      if (next == '[') {
        if (body.starts_with("<") && (final_ch == 'M' || final_ch == 'm')) {
          return "\x1b[" + body + final_ch;
        }
        if (final_ch == 'u') {
          int cp = 0;
          int mods = 1;
          std::sscanf(body.c_str(), "%d;%d", &cp, &mods);
          if ((mods & 4) != 0 && (cp == 'c' || cp == 'C')) return "Ctrl+C";
          if (cp == 27) return "Escape";
          if (cp == 13 || cp == 10) return "Enter";
          if (cp == 9) return "Tab";
          if (cp == 127 || cp == 8) return "Backspace";
          if (cp >= 32 && cp < 127)
            return std::string(1, static_cast<char>(cp));
          return "";
        }
        if (final_ch == 'A') return "ArrowUp";
        if (final_ch == 'B') return "ArrowDown";
        if (final_ch == 'C') return "ArrowRight";
        if (final_ch == 'D') return "ArrowLeft";
        if (final_ch == 'H') return "Home";
        if (final_ch == 'F') return "End";
        if (final_ch == '~') {
          if (body == "1" || body == "7") return "Home";
          if (body == "4" || body == "8") return "End";
          if (body == "2") return "Insert";
          if (body == "3") return "Delete";
          if (body == "5") return "PageUp";
          if (body == "6") return "PageDown";
        }
      } else if (next == 'O') {
        if (final_ch == 'A') return "ArrowUp";
        if (final_ch == 'B') return "ArrowDown";
        if (final_ch == 'C') return "ArrowRight";
        if (final_ch == 'D') return "ArrowLeft";
        if (final_ch == 'H') return "Home";
        if (final_ch == 'F') return "End";
      }
      return "";
    }
    return "Escape";
  }

  if (ch >= 32 && ch < 127) return std::string(1, static_cast<char>(ch));

  // Decode multi-byte UTF-8 character if present.
  std::string utf8(1, static_cast<char>(ch));
  size_t extra = 0;
  if ((ch & 0xE0) == 0xC0) {
    extra = 1;
  } else if ((ch & 0xF0) == 0xE0) {
    extra = 2;
  } else if ((ch & 0xF8) == 0xF0) {
    extra = 3;
  }
  while (extra > 0 && !q.empty()) {
    utf8.push_back(static_cast<char>(q.front()));
    q.pop_front();
    --extra;
  }
  return utf8;
}

}  // namespace

std::string ReadTerminalKey(int timeout_ms) {
  auto& q = PendingKeyBytes();
  if (!q.empty()) {
    std::string k = PopDecodedKeyFromQueue();
    if (!k.empty()) return k;
  }

  auto pipe = perception::GetFileDescriptorPipe(STDIN_FILENO);
  auto start_time = perception::GetTimeSinceKernelStarted();

  for (;;) {
    if (pipe != nullptr) {
      char buf[64];
      long n = pipe->Read(buf, sizeof(buf), true);
      if (n > 0) {
        for (long i = 0; i < n; ++i)
          q.push_back(static_cast<unsigned char>(buf[i]));
      }
    } else {
      char buf[64];
      ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
      if (n > 0) {
        for (ssize_t i = 0; i < n; ++i)
          q.push_back(static_cast<unsigned char>(buf[i]));
      }
    }

    while (!q.empty()) {
      std::string k = PopDecodedKeyFromQueue();
      if (!k.empty()) return k;
    }

    if (timeout_ms == 0) return "";
    if (timeout_ms > 0) {
      auto elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              perception::GetTimeSinceKernelStarted() - start_time)
              .count();
      if (elapsed_ms >= timeout_ms) return "";
    }

    perception::SleepForDuration(
        std::chrono::milliseconds(kKeyPollIntervalMs));
  }
}

void RegisterSysModule(JSContext* ctx, JsEngine& engine) {
  JSValue global_obj = JS_GetGlobalObject(ctx);

  // Build `sys` namespace.
  JSValue sys_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, sys_obj, "memory",
                    JS_NewCFunction(ctx, &JsSysMemory, "memory", 0));
  JS_SetPropertyStr(ctx, sys_obj, "cores",
                    JS_NewCFunction(ctx, &JsSysCores, "cores", 0));
  JS_SetPropertyStr(ctx, sys_obj, "uptime",
                    JS_NewCFunction(ctx, &JsSysUptime, "uptime", 0));
  JS_SetPropertyStr(ctx, sys_obj, "sleep",
                    JS_NewCFunction(ctx, &JsSysSleep, "sleep", 1));
  JS_SetPropertyStr(ctx, sys_obj, "exit",
                    JS_NewCFunction(ctx, &JsSysExit, "exit", 1));
  JS_SetPropertyStr(ctx, sys_obj, "gc",
                    JS_NewCFunction(ctx, &JsSysGc, "gc", 0));
  JS_SetPropertyStr(ctx, sys_obj, "profile",
                    JS_NewCFunction(ctx, &JsSysProfile, "profile", 1));
  JS_SetPropertyStr(ctx, sys_obj, "mounts",
                    JS_NewCFunction(ctx, &JsSysMounts, "mounts", 0));
  JS_SetPropertyStr(ctx, sys_obj, "mount",
                    JS_NewCFunction(ctx, &JsSysMount, "mount", 3));
  JS_SetPropertyStr(ctx, sys_obj, "unmount",
                    JS_NewCFunction(ctx, &JsSysUnmount, "unmount", 1));
  JS_SetPropertyStr(
      ctx, sys_obj, "setMountPath",
      JS_NewCFunction(ctx, &JsSysSetMountPath, "setMountPath", 2));
  JS_SetPropertyStr(
      ctx, sys_obj, "remapMount",
      JS_NewCFunction(ctx, &JsSysSetMountPath, "remapMount", 2));
  JS_SetPropertyStr(ctx, sys_obj, "powerOff",
                    JS_NewCFunction(ctx, &JsSysPowerOff, "powerOff", 0));
  JS_SetPropertyStr(ctx, sys_obj, "restart",
                    JS_NewCFunction(ctx, &JsSysRestart, "restart", 0));
  JS_SetPropertyStr(ctx, sys_obj, "suspend",
                    JS_NewCFunction(ctx, &JsSysSuspend, "suspend", 0));
  JS_SetPropertyStr(ctx, sys_obj, "wake",
                    JS_NewCFunction(ctx, &JsSysWake, "wake", 0));

  JSValue args_arr = JS_NewArray(ctx);
  for (size_t i = 0; i < engine.ScriptArgs().size(); ++i) {
    JS_SetPropertyUint32(ctx, args_arr, static_cast<uint32_t>(i),
                         JS_NewString(ctx, engine.ScriptArgs()[i].c_str()));
  }
  JS_SetPropertyStr(ctx, sys_obj, "args", args_arr);

  JSValue env_obj = JS_NewObject(ctx);
  if (environ != nullptr) {
    for (char** ep = environ; *ep != nullptr; ++ep) {
      std::string_view entry(*ep);
      size_t eq = entry.find('=');
      if (eq != std::string_view::npos) {
        std::string key(entry.substr(0, eq));
        std::string val(entry.substr(eq + 1));
        JS_SetPropertyStr(ctx, env_obj, key.c_str(),
                          JS_NewStringLen(ctx, val.data(), val.size()));
      }
    }
  }
  JS_SetPropertyStr(ctx, sys_obj, "env", env_obj);
  JS_SetPropertyStr(ctx, global_obj, "sys", sys_obj);

  // Build `registry` namespace.
  JSValue reg_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(
      ctx, reg_obj, "namespaces",
      JS_NewCFunction(ctx, &JsRegistryNamespaces, "namespaces", 1));
  JS_SetPropertyStr(ctx, reg_obj, "keys",
                    JS_NewCFunction(ctx, &JsRegistryKeys, "keys", 2));
  JS_SetPropertyStr(ctx, reg_obj, "get",
                    JS_NewCFunction(ctx, &JsRegistryGet, "get", 3));
  JS_SetPropertyStr(ctx, reg_obj, "set",
                    JS_NewCFunction(ctx, &JsRegistrySet, "set", 4));
  JS_SetPropertyStr(ctx, reg_obj, "delete",
                    JS_NewCFunction(ctx, &JsRegistryDelete, "delete", 3));
  JS_SetPropertyStr(ctx, reg_obj, "remove",
                    JS_NewCFunction(ctx, &JsRegistryDelete, "remove", 3));
  JS_SetPropertyStr(ctx, reg_obj, "flush",
                    JS_NewCFunction(ctx, &JsRegistryFlush, "flush", 0));
  JS_SetPropertyStr(ctx, global_obj, "registry", reg_obj);

  // Build `clipboard` namespace.
  JSValue clip_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, clip_obj, "get",
                    JS_NewCFunction(ctx, &JsClipboardGet, "get", 0));
  JS_SetPropertyStr(ctx, clip_obj, "set",
                    JS_NewCFunction(ctx, &JsClipboardSet, "set", 1));
  JS_SetPropertyStr(ctx, global_obj, "clipboard", clip_obj);

  // Build `term.style` and `term` namespace.
  JSValue style_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, style_obj, "bold",
                    JS_NewCFunction(ctx, &JsStyleBold, "bold", 1));
  JS_SetPropertyStr(ctx, style_obj, "dim",
                    JS_NewCFunction(ctx, &JsStyleDim, "dim", 1));
  JS_SetPropertyStr(ctx, style_obj, "italic",
                    JS_NewCFunction(ctx, &JsStyleItalic, "italic", 1));
  JS_SetPropertyStr(ctx, style_obj, "underline",
                    JS_NewCFunction(ctx, &JsStyleUnderline, "underline", 1));
  JS_SetPropertyStr(ctx, style_obj, "red",
                    JS_NewCFunction(ctx, &JsStyleRed, "red", 1));
  JS_SetPropertyStr(ctx, style_obj, "green",
                    JS_NewCFunction(ctx, &JsStyleGreen, "green", 1));
  JS_SetPropertyStr(ctx, style_obj, "yellow",
                    JS_NewCFunction(ctx, &JsStyleYellow, "yellow", 1));
  JS_SetPropertyStr(ctx, style_obj, "blue",
                    JS_NewCFunction(ctx, &JsStyleBlue, "blue", 1));
  JS_SetPropertyStr(ctx, style_obj, "magenta",
                    JS_NewCFunction(ctx, &JsStyleMagenta, "magenta", 1));
  JS_SetPropertyStr(ctx, style_obj, "cyan",
                    JS_NewCFunction(ctx, &JsStyleCyan, "cyan", 1));
  JS_SetPropertyStr(ctx, style_obj, "white",
                    JS_NewCFunction(ctx, &JsStyleWhite, "white", 1));
  JS_SetPropertyStr(ctx, style_obj, "gray",
                    JS_NewCFunction(ctx, &JsStyleGray, "gray", 1));
  JS_SetPropertyStr(ctx, style_obj, "bgRed",
                    JS_NewCFunction(ctx, &JsStyleBgRed, "bgRed", 1));
  JS_SetPropertyStr(ctx, style_obj, "bgGreen",
                    JS_NewCFunction(ctx, &JsStyleBgGreen, "bgGreen", 1));
  JS_SetPropertyStr(ctx, style_obj, "bgYellow",
                    JS_NewCFunction(ctx, &JsStyleBgYellow, "bgYellow", 1));
  JS_SetPropertyStr(ctx, style_obj, "bgBlue",
                    JS_NewCFunction(ctx, &JsStyleBgBlue, "bgBlue", 1));
  JS_SetPropertyStr(ctx, style_obj, "rgb",
                    JS_NewCFunction(ctx, &JsStyleRgb, "rgb", 4));
  JS_SetPropertyStr(ctx, style_obj, "bgRgb",
                    JS_NewCFunction(ctx, &JsStyleBgRgb, "bgRgb", 4));

  JSValue term_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, term_obj, "clear",
                    JS_NewCFunction(ctx, &JsTermClear, "clear", 0));
  JS_SetPropertyStr(ctx, term_obj, "title",
                    JS_NewCFunction(ctx, &JsTermTitle, "title", 1));
  JS_SetPropertyStr(ctx, term_obj, "write",
                    JS_NewCFunction(ctx, &JsTermWrite, "write", 1));
  JS_SetPropertyStr(ctx, term_obj, "size",
                    JS_NewCFunction(ctx, &JsTermSize, "size", 0));
  JS_SetPropertyStr(ctx, term_obj, "moveTo",
                    JS_NewCFunction(ctx, &JsTermMoveTo, "moveTo", 2));
  JS_SetPropertyStr(ctx, term_obj, "cursor",
                    JS_NewCFunction(ctx, &JsTermCursor, "cursor", 2));
  JS_SetPropertyStr(ctx, term_obj, "hideCursor",
                    JS_NewCFunction(ctx, &JsTermHideCursor, "hideCursor", 0));
  JS_SetPropertyStr(ctx, term_obj, "showCursor",
                    JS_NewCFunction(ctx, &JsTermShowCursor, "showCursor", 0));
  JS_SetPropertyStr(ctx, term_obj, "altScreen",
                    JS_NewCFunction(ctx, &JsTermAltScreen, "altScreen", 1));
  JS_SetPropertyStr(ctx, term_obj, "rawMode",
                    JS_NewCFunction(ctx, &JsTermRawMode, "rawMode", 1));
  JS_SetPropertyStr(ctx, term_obj, "mouse",
                    JS_NewCFunction(ctx, &JsTermMouse, "mouse", 1));
  JS_SetPropertyStr(ctx, term_obj, "readKey",
                    JS_NewCFunction(ctx, &JsTermReadKey, "readKey", 1));
  JS_SetPropertyStr(ctx, term_obj, "table",
                    JS_NewCFunction(ctx, &JsTermTable, "table", 1));
  JS_SetPropertyStr(ctx, term_obj, "inspect",
                    JS_NewCFunction(ctx, &JsTermInspect, "inspect", 2));
  JS_SetPropertyStr(ctx, term_obj, "memory",
                    JS_NewCFunction(ctx, &JsTermMemory, "memory", 0));
  JS_SetPropertyStr(ctx, term_obj, "style", style_obj);
  JS_SetPropertyStr(ctx, global_obj, "term", term_obj);

  // Build global `sleep`, `print`, and `console`.
  JS_SetPropertyStr(ctx, global_obj, "sleep",
                    JS_NewCFunction(ctx, &JsSysSleep, "sleep", 1));
  JS_SetPropertyStr(ctx, global_obj, "print",
                    JS_NewCFunction(ctx, &JsGlobalPrint, "print", 1));

  JSValue console_obj = JS_NewObject(ctx);
  JS_SetPropertyStr(ctx, console_obj, "log",
                    JS_NewCFunction(ctx, &JsGlobalPrint, "log", 1));
  JS_SetPropertyStr(ctx, console_obj, "info",
                    JS_NewCFunction(ctx, &JsGlobalPrint, "info", 1));
  JS_SetPropertyStr(ctx, console_obj, "warn",
                    JS_NewCFunction(ctx, &JsConsoleWarn, "warn", 1));
  JS_SetPropertyStr(ctx, console_obj, "error",
                    JS_NewCFunction(ctx, &JsConsoleError, "error", 1));
  JS_SetPropertyStr(ctx, console_obj, "dir",
                    JS_NewCFunction(ctx, &JsGlobalPrint, "dir", 1));
  JS_SetPropertyStr(ctx, console_obj, "table",
                    JS_NewCFunction(ctx, &JsTermTable, "table", 1));
  JS_SetPropertyStr(ctx, console_obj, "clear",
                    JS_NewCFunction(ctx, &JsTermClear, "clear", 0));
  JS_SetPropertyStr(ctx, console_obj, "time",
                    JS_NewCFunction(ctx, &JsConsoleTime, "time", 1));
  JS_SetPropertyStr(ctx, console_obj, "timeEnd",
                    JS_NewCFunction(ctx, &JsConsoleTimeEnd, "timeEnd", 1));
  JS_SetPropertyStr(ctx, global_obj, "console", console_obj);

  JS_FreeValue(ctx, global_obj);
}

}  // namespace module
