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

#include "js_engine.h"

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "api_catalog.h"
#include "completion.h"
#include "module/fs.h"
#include "module/net.h"
#include "module/proc.h"
#include "module/sys.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/storage_manager.h"
#include "perception/time.h"
#include "slash_commands.h"

namespace {

// Fallback terminal width in columns when TIOCGWINSZ is unavailable.
constexpr int kDefaultTerminalCols = 80;

// Fallback terminal height in rows when TIOCGWINSZ is unavailable.
constexpr int kDefaultTerminalRows = 25;

// Poll timeout in milliseconds when querying DSR cursor position (`\x1b[6n`).
constexpr int kCursorQueryTimeoutMs = 40;

// Sleep interval in milliseconds while waiting for pending Promises/fibers.
constexpr int kAsyncPollSleepMs = 2;

// Maximum prototype chain depth traversed during property completion.
constexpr int kMaxPrototypeDepth = 6;

// Maximum directory entries read per ReadDirectory RPC call.
constexpr uint64_t kReadDirBatchSize = 256;

// Path to the bundled API & Cookbook Markdown file opened by `/help`.
constexpr std::string_view kApiMarkdownPath = "/Applications/jsshell/api.md";

// Virtual filename used for inline `--script` evaluation.
constexpr std::string_view kInlineScriptFilename = "<script>";

// ANSI escape sequence to reset styles.
constexpr std::string_view kAnsiReset = "\x1b[0m";

// ANSI color for string values in previews.
constexpr std::string_view kAnsiGreen = "\x1b[38;2;166;227;161m";

// ANSI color for numeric and boolean values in previews.
constexpr std::string_view kAnsiYellow = "\x1b[38;2;249;226;175m";

// ANSI color for object keys and identifiers in previews.
constexpr std::string_view kAnsiBlue = "\x1b[38;2;137;180;250m";

// ANSI color for null/undefined/dim badges in previews.
constexpr std::string_view kAnsiDim = "\x1b[38;2;108;112;134m";

// ANSI color for error messages.
constexpr std::string_view kAnsiRed = "\x1b[1;38;2;243;139;168m";

// ANSI color for cyan accents.
constexpr std::string_view kAnsiCyan = "\x1b[38;2;148;226;213m";

// JavaScript keywords offered during top-level identifier completion.
constexpr std::string_view kCompletionKeywords[] = {
    "await", "let", "const", "function", "async", "for",
    "while", "if",  "else",  "return",   "try",   "catch",
    "true",  "false", "null"};

int OnQuickJsInterrupt(JSRuntime* rt, void* opaque) {
  auto* engine = static_cast<JsEngine*>(opaque);
  if (engine != nullptr && engine->IsInterruptRequested()) return 1;
  (void)rt;
  return 0;
}

bool ReadFileToString(const std::string& path, std::string& content_out) {
  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream ss;
  ss << file.rdbuf();
  content_out = ss.str();
  return true;
}

std::string DirnameOfPath(std::string_view path) {
  size_t slash = path.rfind('/');
  if (slash == std::string_view::npos) return "/";
  if (slash == 0) return "/";
  return std::string(path.substr(0, slash));
}

char* NormalizeJsModuleName(JSContext* ctx, const char* base_name,
                            const char* module_name, void* opaque) {
  auto* engine = static_cast<JsEngine*>(opaque);
  std::string base_dir =
      (engine != nullptr) ? engine->Cwd() : std::string("/");
  if (base_name != nullptr && base_name[0] == '/')
    base_dir = DirnameOfPath(base_name);
  std::string normalized = NormalizeLexicalPath(base_dir, module_name);
  return js_strdup(ctx, normalized.c_str());
}

JSModuleDef* LoadJsModuleFromFile(JSContext* ctx, const char* module_name,
                                  void* opaque) {
  (void)opaque;
  std::string source;
  if (!ReadFileToString(module_name, source)) {
    JS_ThrowReferenceError(ctx, "could not load module '%s'", module_name);
    return nullptr;
  }

  JSValue func_val =
      JS_Eval(ctx, source.c_str(), source.size(), module_name,
              JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
  if (JS_IsException(func_val)) return nullptr;

  auto* mod = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
  JSValue meta_obj = JS_GetImportMeta(ctx, mod);
  if (!JS_IsException(meta_obj)) {
    std::string url = std::string("file://") + module_name;
    JS_DefinePropertyValueStr(ctx, meta_obj, "url",
                              JS_NewString(ctx, url.c_str()), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main", JS_NewBool(ctx, 0),
                              JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta_obj);
  }
  JS_FreeValue(ctx, func_val);
  return mod;
}

void PrintJsErrorValue(JSContext* ctx, JSValueConst error_val) {
  const char* msg = JS_ToCString(ctx, error_val);
  std::cerr << kAnsiRed;
  if (msg != nullptr) {
    std::cerr << msg;
    JS_FreeCString(ctx, msg);
  } else {
    std::cerr << "[Exception]";
  }
  std::cerr << kAnsiReset << "\n";

  if (JS_IsObject(error_val)) {
    JSValue stack = JS_GetPropertyStr(ctx, error_val, "stack");
    if (!JS_IsUndefined(stack) && !JS_IsNull(stack) &&
        !JS_IsException(stack)) {
      const char* stack_str = JS_ToCString(ctx, stack);
      if (stack_str != nullptr && stack_str[0] != '\0')
        std::cerr << kAnsiDim << stack_str << kAnsiReset;
      if (stack_str != nullptr &&
          stack_str[std::strlen(stack_str) > 0 ? std::strlen(stack_str) - 1
                                               : 0] != '\n')
        std::cerr << "\n";
      if (stack_str != nullptr) JS_FreeCString(ctx, stack_str);
    }
    JS_FreeValue(ctx, stack);
  }
  std::cerr << std::flush;
}

void DumpCurrentException(JSContext* ctx) {
  JSValue exc = JS_GetException(ctx);
  PrintJsErrorValue(ctx, exc);
  JS_FreeValue(ctx, exc);
}

bool StartsWithCaseInsensitive(std::string_view text, std::string_view prefix) {
  if (prefix.size() > text.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = static_cast<char>(
        std::tolower(static_cast<unsigned char>(text[i])));
    char b = static_cast<char>(
        std::tolower(static_cast<unsigned char>(prefix[i])));
    if (a != b) return false;
  }
  return true;
}

std::string EscapeJsStringLiteral(std::string_view input) {
  std::string out = "\"";
  for (char ch : input) {
    if (ch == '\\') {
      out += "\\\\";
    } else if (ch == '"') {
      out += "\\\"";
    } else if (ch == '\n') {
      out += "\\n";
    } else if (ch == '\r') {
      out += "\\r";
    } else if (ch == '\t') {
      out += "\\t";
    } else {
      out.push_back(ch);
    }
  }
  out.push_back('"');
  return out;
}

std::vector<std::string> SplitLinesPreservingContent(std::string_view text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t nl = text.find('\n', start);
    if (nl == std::string_view::npos) {
      lines.emplace_back(text.substr(start));
      break;
    }
    lines.emplace_back(text.substr(start, nl - start));
    start = nl + 1;
  }
  if (lines.empty()) lines.emplace_back("");
  return lines;
}

std::string ColorizeJsonPreview(std::string_view json) {
  std::string out;
  out.reserve(json.size() * 2);
  size_t i = 0;
  while (i < json.size()) {
    char ch = json[i];
    if (ch == '"') {
      size_t end = i + 1;
      while (end < json.size()) {
        if (json[end] == '\\' && end + 1 < json.size()) {
          end += 2;
          continue;
        }
        if (json[end] == '"') {
          ++end;
          break;
        }
        ++end;
      }
      size_t lookahead = end;
      while (lookahead < json.size() && json[lookahead] == ' ') ++lookahead;
      bool is_key = (lookahead < json.size() && json[lookahead] == ':');
      out += is_key ? kAnsiBlue : kAnsiGreen;
      out.append(json.substr(i, end - i));
      out += kAnsiReset;
      i = end;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch)) ||
        (ch == '-' && i + 1 < json.size() &&
         std::isdigit(static_cast<unsigned char>(json[i + 1])))) {
      size_t end = i + 1;
      while (end < json.size() &&
             (std::isalnum(static_cast<unsigned char>(json[end])) ||
              json[end] == '.' || json[end] == '+' || json[end] == '-')) {
        ++end;
      }
      out += kAnsiYellow;
      out.append(json.substr(i, end - i));
      out += kAnsiReset;
      i = end;
      continue;
    }
    if (json.substr(i).starts_with("true") ||
        json.substr(i).starts_with("false")) {
      size_t len = json.substr(i).starts_with("true") ? 4 : 5;
      out += kAnsiYellow;
      out.append(json.substr(i, len));
      out += kAnsiReset;
      i += len;
      continue;
    }
    if (json.substr(i).starts_with("null")) {
      out += kAnsiDim;
      out.append("null");
      out += kAnsiReset;
      i += 4;
      continue;
    }
    out.push_back(ch);
    ++i;
  }
  return out;
}

void AppendPathCompletions(std::string_view cwd, std::string_view prefix,
                           bool directories_only, bool quote_if_spaces,
                           bool add_closing_quote,
                           std::vector<CompletionItem>& out) {
  std::string dir_part;
  std::string name_prefix;
  size_t last_slash = prefix.rfind('/');
  if (last_slash == std::string_view::npos) {
    dir_part = "";
    name_prefix = std::string(prefix);
  } else {
    dir_part = std::string(prefix.substr(0, last_slash + 1));
    name_prefix = std::string(prefix.substr(last_slash + 1));
  }

  std::string resolved_dir =
      dir_part.empty() ? std::string(cwd) : NormalizeLexicalPath(cwd, dir_part);

  perception::ReadDirectoryRequest req;
  req.path = resolved_dir;
  req.first_index = 0;
  req.maximum_number_of_entries = kReadDirBatchSize;
  auto status_or_resp =
      perception::GetService<perception::StorageManager>().ReadDirectory(req);
  if (!status_or_resp.Ok()) return;

  for (const auto& entry : status_or_resp->entries) {
    bool is_dir = (entry.type == perception::DirectoryEntry::Type::DIRECTORY);
    if (directories_only && !is_dir) continue;
    if (!StartsWithCaseInsensitive(entry.name, name_prefix)) continue;

    std::string candidate = dir_part + entry.name + (is_dir ? "/" : "");
    std::string insert = candidate;
    if (quote_if_spaces && candidate.find(' ') != std::string::npos) {
      insert = "\"" + candidate + (is_dir ? "" : "\"");
    } else if (add_closing_quote && !is_dir) {
      insert += "\"";
    }

    CompletionItem item;
    item.insert_text = insert;
    item.display_text = entry.name + (is_dir ? "/" : "");
    item.signature = is_dir ? "directory" : "file";
    item.description = NormalizeLexicalPath(cwd, candidate);
    out.push_back(std::move(item));
  }
}

}  // namespace

JsEngine::JsEngine() {
  rt_ = JS_NewRuntime();
  JS_SetMaxStackSize(rt_, 0);
  JS_SetRuntimeOpaque(rt_, this);
  JS_SetInterruptHandler(rt_, &OnQuickJsInterrupt, this);
  JS_SetModuleLoaderFunc(rt_, &NormalizeJsModuleName, &LoadJsModuleFromFile,
                         this);
  InitializeContext();
}

JsEngine::~JsEngine() {
  FreeHistoryValues();
  if (ctx_ != nullptr) {
    JS_FreeContext(ctx_);
    ctx_ = nullptr;
  }
  if (rt_ != nullptr) {
    JS_FreeRuntime(rt_);
    rt_ = nullptr;
  }
}

void JsEngine::InitializeContext() {
  ctx_ = JS_NewContext(rt_);
  JS_SetContextOpaque(ctx_, this);

  module::RegisterFsModule(ctx_, *this);
  module::RegisterProcModule(ctx_, *this);
  module::RegisterSysModule(ctx_, *this);
  module::RegisterNetModule(ctx_, *this);

  JSValue global_obj = JS_GetGlobalObject(ctx_);
  JS_SetPropertyStr(ctx_, global_obj, "_", JS_UNDEFINED);
  JS_FreeValue(ctx_, global_obj);

  SetScriptArgs(script_args_);
}

void JsEngine::FreeHistoryValues() {
  for (auto& block : history_) {
    if (block.has_js_value && ctx_ != nullptr) {
      JS_FreeValue(ctx_, block.js_value);
      block.js_value = JS_UNDEFINED;
      block.has_js_value = false;
    }
  }
}

void JsEngine::ResetSession() {
  FreeHistoryValues();
  history_.clear();
  jobs_.clear();
  next_job_id_ = 1;
  interrupt_requested_ = false;

  if (ctx_ != nullptr) {
    JS_FreeContext(ctx_);
    ctx_ = nullptr;
  }
  if (rt_ != nullptr) JS_RunGC(rt_);
  InitializeContext();
}

void JsEngine::SetScriptArgs(const std::vector<std::string>& args) {
  script_args_ = args;
  if (ctx_ == nullptr) return;

  JSValue global_obj = JS_GetGlobalObject(ctx_);
  JSValue sys_obj = JS_GetPropertyStr(ctx_, global_obj, "sys");
  if (JS_IsObject(sys_obj)) {
    JSValue arr = JS_NewArray(ctx_);
    for (size_t i = 0; i < script_args_.size(); ++i) {
      JS_SetPropertyUint32(ctx_, arr, static_cast<uint32_t>(i),
                           JS_NewString(ctx_, script_args_[i].c_str()));
    }
    JS_SetPropertyStr(ctx_, sys_obj, "args", arr);
  }
  JS_FreeValue(ctx_, sys_obj);
  JS_FreeValue(ctx_, global_obj);
}

void JsEngine::DrainMicrotasks() {
  if (rt_ == nullptr) return;
  JS_UpdateStackTop(rt_);
  for (;;) {
    JSContext* job_ctx = nullptr;
    int err = JS_ExecutePendingJob(rt_, &job_ctx);
    if (err < 0) {
      if (job_ctx != nullptr) DumpCurrentException(job_ctx);
      break;
    }
    if (err == 0) break;
  }
  perception::FinishAnyPendingWork();
}

JSValue JsEngine::AwaitValue(JSValue val, bool& had_exception) {
  if (JS_IsException(val)) {
    DumpCurrentException(ctx_);
    had_exception = true;
    return JS_UNDEFINED;
  }

  for (;;) {
    DrainMicrotasks();

    int promise_state = JS_PromiseState(ctx_, val);
    if (promise_state < 0 && JS_IsObject(val)) {
      JSValue then_fn = JS_GetPropertyStr(ctx_, val, "then");
      if (JS_IsException(then_fn)) {
        JS_FreeValue(ctx_, val);
        DumpCurrentException(ctx_);
        had_exception = true;
        return JS_UNDEFINED;
      }
      if (JS_IsFunction(ctx_, then_fn)) {
        JSValue resolving_funcs[2] = {JS_UNDEFINED, JS_UNDEFINED};
        JSValue promise = JS_NewPromiseCapability(ctx_, resolving_funcs);
        if (JS_IsException(promise)) {
          JS_FreeValue(ctx_, then_fn);
          JS_FreeValue(ctx_, val);
          DumpCurrentException(ctx_);
          had_exception = true;
          return JS_UNDEFINED;
        }
        JSValue call_ret = JS_Call(ctx_, then_fn, val, 2, resolving_funcs);
        JS_FreeValue(ctx_, then_fn);
        JS_FreeValue(ctx_, resolving_funcs[0]);
        JS_FreeValue(ctx_, resolving_funcs[1]);
        JS_FreeValue(ctx_, val);
        if (JS_IsException(call_ret)) {
          JS_FreeValue(ctx_, promise);
          DumpCurrentException(ctx_);
          had_exception = true;
          return JS_UNDEFINED;
        }
        JS_FreeValue(ctx_, call_ret);
        val = promise;
        continue;
      }
      JS_FreeValue(ctx_, then_fn);
      break;
    }

    if (promise_state < 0) break;

    if (promise_state == JS_PROMISE_FULFILLED) {
      JSValue result = JS_PromiseResult(ctx_, val);
      JS_FreeValue(ctx_, val);
      val = result;
      continue;
    }

    if (promise_state == JS_PROMISE_REJECTED) {
      JSValue reason = JS_PromiseResult(ctx_, val);
      JS_FreeValue(ctx_, val);
      PrintJsErrorValue(ctx_, reason);
      JS_FreeValue(ctx_, reason);
      had_exception = true;
      return JS_UNDEFINED;
    }

    if (interrupt_requested_) {
      JS_FreeValue(ctx_, val);
      std::cerr << kAnsiRed << "Interrupted" << kAnsiReset << "\n"
                << std::flush;
      had_exception = true;
      return JS_UNDEFINED;
    }

    JS_UpdateStackTop(rt_);
    JSContext* job_ctx = nullptr;
    int err = JS_ExecutePendingJob(rt_, &job_ctx);
    if (err < 0 && job_ctx != nullptr) DumpCurrentException(job_ctx);
    perception::FinishAnyPendingWork();

    if (err <= 0 && JS_PromiseState(ctx_, val) == JS_PROMISE_PENDING) {
      perception::SleepForDuration(
          std::chrono::milliseconds(kAsyncPollSleepMs));
      perception::FinishAnyPendingWork();
    }
  }

  DrainMicrotasks();
  return val;
}

JSValue JsEngine::EvaluateAsync(std::string_view code,
                                std::string_view filename, bool is_repl,
                                bool& had_exception) {
  had_exception = false;
  ClearInterrupt();
  JS_UpdateStackTop(rt_);

  std::string source = is_repl ? TransformReplTopLevelDeclarations(code)
                               : std::string(code);
  std::string fname(filename);

  if (!is_repl && JS_DetectModule(source.c_str(), source.size())) {
    JSValue mod_obj =
        JS_Eval(ctx_, source.c_str(), source.size(), fname.c_str(),
                JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(mod_obj)) {
      DumpCurrentException(ctx_);
      had_exception = true;
      return JS_UNDEFINED;
    }
    auto* mod = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(mod_obj));
    JSValue meta_obj = JS_GetImportMeta(ctx_, mod);
    if (!JS_IsException(meta_obj)) {
      std::string url = std::string("file://") + fname;
      JS_DefinePropertyValueStr(ctx_, meta_obj, "url",
                                JS_NewString(ctx_, url.c_str()), JS_PROP_C_W_E);
      JS_DefinePropertyValueStr(ctx_, meta_obj, "main", JS_NewBool(ctx_, 1),
                                JS_PROP_C_W_E);
      JS_FreeValue(ctx_, meta_obj);
    }
    JSValue eval_res = JS_EvalFunction(ctx_, mod_obj);
    JSValue settled = AwaitValue(eval_res, had_exception);
    JS_FreeValue(ctx_, settled);
    return JS_UNDEFINED;
  }

  JSValue async_promise = JS_Eval(
      ctx_, source.c_str(), source.size(), fname.c_str(),
      JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_ASYNC | JS_EVAL_FLAG_BACKTRACE_BARRIER);
  JSValue outer_res = AwaitValue(async_promise, had_exception);
  if (had_exception) return JS_UNDEFINED;

  JSValue inner_val = JS_UNDEFINED;
  if (JS_IsObject(outer_res)) {
    inner_val = JS_GetPropertyStr(ctx_, outer_res, "value");
    JS_FreeValue(ctx_, outer_res);
  } else {
    inner_val = outer_res;
  }

  return AwaitValue(inner_val, had_exception);
}

bool JsEngine::RunScriptFile(std::string_view path,
                             const std::vector<std::string>& args) {
  SetScriptArgs(args);
  std::string normalized = NormalizeLexicalPath(cwd_, path);
  std::string source;
  if (!ReadFileToString(normalized, source)) {
    std::cerr << kAnsiRed << "Error: Could not open script file '"
              << normalized << "'" << kAnsiReset << "\n"
              << std::flush;
    return false;
  }

  bool had_exception = false;
  JSValue res = EvaluateAsync(source, normalized, false, had_exception);
  JS_FreeValue(ctx_, res);
  DrainMicrotasks();
  return !had_exception;
}

bool JsEngine::RunInlineScript(std::string_view code) {
  bool had_exception = false;
  JSValue res =
      EvaluateAsync(code, kInlineScriptFilename, true, had_exception);
  if (!had_exception && !JS_IsUndefined(res)) {
    if (JS_IsString(res)) {
      size_t len = 0;
      const char* str = JS_ToCStringLen(ctx_, &len, res);
      if (str != nullptr) {
        std::cout.write(str, static_cast<std::streamsize>(len));
        if (len == 0 || str[len - 1] != '\n') std::cout << "\n";
        std::cout << std::flush;
        JS_FreeCString(ctx_, str);
      }
    } else {
      std::cout << FormatJsValuePreview(res, true, 24) << "\n" << std::flush;
    }
  }
  JS_FreeValue(ctx_, res);
  DrainMicrotasks();
  return !had_exception;
}

std::string JsEngine::FormatJsValuePreview(JSValue val, bool colorize,
                                           int max_lines) {
  if (JS_IsUndefined(val))
    return colorize ? std::string(kAnsiDim) + "undefined" + std::string(kAnsiReset)
                    : "undefined";
  if (JS_IsNull(val))
    return colorize ? std::string(kAnsiDim) + "null" + std::string(kAnsiReset)
                    : "null";
  if (JS_IsBool(val)) {
    std::string text = JS_ToBool(ctx_, val) ? "true" : "false";
    return colorize ? std::string(kAnsiYellow) + text + std::string(kAnsiReset)
                    : text;
  }
  if (JS_IsNumber(val) || JS_IsBigInt(ctx_, val)) {
    const char* cstr = JS_ToCString(ctx_, val);
    std::string text = (cstr != nullptr) ? cstr : "0";
    if (cstr != nullptr) JS_FreeCString(ctx_, cstr);
    if (JS_IsBigInt(ctx_, val)) text += "n";
    return colorize ? std::string(kAnsiYellow) + text + std::string(kAnsiReset)
                    : text;
  }
  if (JS_IsString(val)) {
    const char* cstr = JS_ToCString(ctx_, val);
    std::string quoted =
        EscapeJsStringLiteral((cstr != nullptr) ? cstr : "");
    if (cstr != nullptr) JS_FreeCString(ctx_, cstr);
    return colorize ? std::string(kAnsiGreen) + quoted + std::string(kAnsiReset)
                    : quoted;
  }
  if (JS_IsFunction(ctx_, val)) {
    JSValue name_val = JS_GetPropertyStr(ctx_, val, "name");
    const char* name_cstr = JS_ToCString(ctx_, name_val);
    std::string label = "[Function";
    if (name_cstr != nullptr && name_cstr[0] != '\0') {
      label += ": ";
      label += name_cstr;
    }
    label += "]";
    if (name_cstr != nullptr) JS_FreeCString(ctx_, name_cstr);
    JS_FreeValue(ctx_, name_val);
    return colorize ? std::string(kAnsiCyan) + label + std::string(kAnsiReset)
                    : label;
  }

  JSValue indent = JS_NewInt32(ctx_, 2);
  JSValue json_val = JS_JSONStringify(ctx_, val, JS_UNDEFINED, indent);
  JS_FreeValue(ctx_, indent);

  std::string raw_json;
  if (!JS_IsException(json_val) && JS_IsString(json_val)) {
    const char* json_cstr = JS_ToCString(ctx_, json_val);
    if (json_cstr != nullptr) {
      raw_json = json_cstr;
      JS_FreeCString(ctx_, json_cstr);
    }
  } else if (JS_IsException(json_val)) {
    JS_FreeValue(ctx_, JS_GetException(ctx_));
  }
  JS_FreeValue(ctx_, json_val);

  if (raw_json.empty()) {
    const char* fallback = JS_ToCString(ctx_, val);
    raw_json = (fallback != nullptr) ? fallback : "[Object]";
    if (fallback != nullptr) JS_FreeCString(ctx_, fallback);
  }

  std::vector<std::string> lines = SplitLinesPreservingContent(raw_json);
  bool truncated = false;
  size_t total_lines = lines.size();
  if (max_lines > 0 && static_cast<int>(lines.size()) > max_lines) {
    lines.resize(static_cast<size_t>(max_lines));
    truncated = true;
  }

  std::string joined;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (i > 0) joined.push_back('\n');
    joined += lines[i];
  }
  if (colorize) joined = ColorizeJsonPreview(joined);

  if (truncated) {
    std::string badge = "  ... (" + std::to_string(total_lines) +
                        " lines total — Up+Enter or Click to inspect)";
    joined.push_back('\n');
    joined += colorize ? (std::string(kAnsiDim) + badge + std::string(kAnsiReset))
                       : badge;
  } else if (JS_IsObject(val) && total_lines > 1 && colorize) {
    joined += std::string(kAnsiDim) +
              "  [Up+Enter or Click to inspect]" + std::string(kAnsiReset);
  }
  return joined;
}

void JsEngine::HandleReplReturnValue(JSValue val, size_t turn_index) {
  if (JS_IsUndefined(val)) return;

  JSValue global_obj = JS_GetGlobalObject(ctx_);
  JS_SetPropertyStr(ctx_, global_obj, "_", JS_DupValue(ctx_, val));
  JS_FreeValue(ctx_, global_obj);

  int cols = kDefaultTerminalCols;
  int rows = kDefaultTerminalRows;
  QueryTerminalSize(cols, rows);

  int start_row = -1;
  int start_col = -1;
  bool have_pos = QueryCursorPosition(start_row, start_col);

  HistoryBlock block;
  block.kind = HistoryBlockKind::kResponse;
  block.turn_index = turn_index;
  block.js_value = JS_DupValue(ctx_, val);
  block.has_js_value = true;

  if (JS_IsString(val)) {
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx_, &len, val);
    std::string raw = (cstr != nullptr) ? std::string(cstr, len) : "";
    if (cstr != nullptr) JS_FreeCString(ctx_, cstr);

    std::cout << raw;
    if (raw.empty() || raw.back() != '\n') std::cout << "\n";
    std::cout << std::flush;

    block.text = raw;
    block.rendered_lines = SplitLinesPreservingContent(raw);
  } else {
    std::string plain = FormatJsValuePreview(val, false, 12);
    std::string colored = FormatJsValuePreview(val, true, 12);
    std::cout << colored << "\n" << std::flush;

    block.text = plain;
    block.rendered_lines = SplitLinesPreservingContent(plain);
  }

  int line_count = std::max(1, static_cast<int>(block.rendered_lines.size()));
  if (have_pos && start_row >= 1) {
    int end_row_unclamped = start_row + line_count - 1;
    int scrolled = std::max(0, end_row_unclamped - rows + 1);
    if (scrolled > 0) NotifyTerminalScrolled(scrolled, rows);
    block.start_screen_row = std::max(1, start_row - scrolled);
    block.end_screen_row =
        std::min(rows, block.start_screen_row + line_count - 1);
  }

  history_.push_back(std::move(block));
}

bool JsEngine::ExecuteSlashCommand(std::string_view line, size_t turn_index,
                                   bool& open_memory_explorer) {
  open_memory_explorer = false;
  ParsedSlashCommand cmd = ParseSlashCommand(line);
  if (!cmd.is_slash_command) return true;

  if (cmd.command == "/exit" || cmd.command == "/quit" || cmd.command == "/q")
    return false;

  if (cmd.command == "/clear") {
    std::cout << "\x1b[2J\x1b[H" << std::flush;
    ClearHistoryScreenCoordinates();
    return true;
  }

  if (cmd.command == "/reset") {
    ResetSession();
    std::cout << kAnsiGreen
              << "✓ QuickJS session reset (variables and history cleared)."
              << kAnsiReset << "\n"
              << std::flush;
    return true;
  }

  if (cmd.command == "/memory" || cmd.command == "/mem") {
    open_memory_explorer = true;
    return true;
  }

  if (cmd.command == "/pwd") {
    std::cout << kAnsiBlue << cwd_ << kAnsiReset << "\n" << std::flush;
    return true;
  }

  if (cmd.command == "/cd") {
    if (cmd.args.empty()) {
      std::cout << kAnsiBlue << cwd_ << kAnsiReset << "\n" << std::flush;
      return true;
    }
    std::string err;
    if (!SetCwd(cmd.args[0], err)) {
      std::cerr << kAnsiRed << "cd: " << err << kAnsiReset << "\n"
                << std::flush;
    } else {
      std::cout << kAnsiBlue << cwd_ << kAnsiReset << "\n" << std::flush;
    }
    return true;
  }

  if (cmd.command == "/ls") {
    std::string expr = cmd.args.empty()
                           ? "fs.readDir()"
                           : ("fs.readDir(" +
                              EscapeJsStringLiteral(cmd.args[0]) + ")");
    bool had_exception = false;
    JSValue val = EvaluateAsync(expr, "<slash:/ls>", true, had_exception);
    if (!had_exception) HandleReplReturnValue(val, turn_index);
    JS_FreeValue(ctx_, val);
    return true;
  }

  if (cmd.command == "/ps") {
    std::string expr =
        cmd.args.empty()
            ? "proc.ps()"
            : ("proc.ps(" + EscapeJsStringLiteral(cmd.args[0]) + ")");
    bool had_exception = false;
    JSValue val = EvaluateAsync(expr, "<slash:/ps>", true, had_exception);
    if (!had_exception) HandleReplReturnValue(val, turn_index);
    JS_FreeValue(ctx_, val);
    return true;
  }

  if (cmd.command == "/kill") {
    if (cmd.args.empty()) {
      std::cerr << kAnsiRed << "Usage: /kill <pid|name>" << kAnsiReset << "\n"
                << std::flush;
      return true;
    }
    bool all_digits = !cmd.args[0].empty();
    for (char ch : cmd.args[0]) {
      if (!std::isdigit(static_cast<unsigned char>(ch))) {
        all_digits = false;
        break;
      }
    }
    std::string arg_expr =
        all_digits ? cmd.args[0] : EscapeJsStringLiteral(cmd.args[0]);
    std::string expr = "proc.kill(" + arg_expr + ")";
    bool had_exception = false;
    JSValue val = EvaluateAsync(expr, "<slash:/kill>", true, had_exception);
    if (!had_exception) HandleReplReturnValue(val, turn_index);
    JS_FreeValue(ctx_, val);
    return true;
  }

  if (cmd.command == "/jobs") {
    const auto& active_jobs = GetBackgroundJobs();
    if (active_jobs.empty()) {
      std::cout << kAnsiDim << "(no background jobs in this session)"
                << kAnsiReset << "\n"
                << std::flush;
      return true;
    }
    for (const auto& job : active_jobs) {
      std::cout << kAnsiBlue << "[" << job.job_id << "] " << kAnsiYellow
                << "PID " << job.pid << " "
                << (job.running ? "\x1b[38;2;166;227;161m(running)"
                                : "\x1b[38;2;108;112;134m(exited)")
                << kAnsiReset << "  " << job.command_line << "\n";
    }
    std::cout << std::flush;
    return true;
  }

  if (cmd.command == "/run") {
    if (cmd.args.empty()) {
      std::cerr << kAnsiRed << "Usage: /run <target> [args...]" << kAnsiReset
                << "\n"
                << std::flush;
      return true;
    }
    std::string target = cmd.args[0];
    std::vector<std::string> rest_args(cmd.args.begin() + 1, cmd.args.end());
    std::string err;
    if (!module::LaunchTargetDirect(*this, target, rest_args, false, err)) {
      std::cerr << kAnsiRed << "run: " << err << kAnsiReset << "\n"
                << std::flush;
    }
    RestoreTerminalState();
    return true;
  }

  if (cmd.command == "/help") {
    std::string err;
    if (module::LaunchTargetDirect(*this, kApiMarkdownPath, {}, true, err)) {
      std::cout << kAnsiGreen << "✓ Opened " << kApiMarkdownPath
                << " in Markdown Viewer." << kAnsiReset << "\n"
                << kAnsiDim
                << "  Tip: Press Tab to autocomplete any namespace (e.g. fs., "
                   "proc., pipe., term.), Up/Down or Click to inspect history, "
                   "Ctrl+M for Memory Explorer."
                << kAnsiReset << "\n"
                << std::flush;
    } else {
      std::cerr << kAnsiRed << "help: " << err << kAnsiReset << "\n"
                << std::flush;
    }
    return true;
  }

  std::cerr << kAnsiRed << "Unknown slash command: " << cmd.command
            << " (type /help for documentation)" << kAnsiReset << "\n"
            << std::flush;
  return true;
}

std::vector<CompletionItem> JsEngine::GetCompletions(std::string_view line,
                                                     size_t cursor_pos) {
  std::vector<CompletionItem> results;
  CompletionContext ctx_info = AnalyzeCompletionContext(line, cursor_pos);
  if (ctx_info.kind == CompletionKind::kNone) return results;

  std::set<std::string> seen;
  auto add_unique = [&](CompletionItem item) {
    if (seen.insert(item.display_text).second)
      results.push_back(std::move(item));
  };

  if (ctx_info.kind == CompletionKind::kSlashCommand) {
    for (const auto& entry : GetSlashCommandCatalog()) {
      if (!StartsWithCaseInsensitive(entry.name, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = entry.name + " ";
      item.display_text = entry.name;
      item.signature = entry.signature;
      item.description = entry.description;
      add_unique(std::move(item));
    }
    return results;
  }

  if (ctx_info.kind == CompletionKind::kSlashArgument) {
    if (ctx_info.receiver == "/run") {
      for (const auto& app : module::DiscoverInstalledApplications()) {
        if (!StartsWithCaseInsensitive(app, ctx_info.prefix)) continue;
        CompletionItem item;
        bool has_space = (app.find(' ') != std::string::npos);
        item.insert_text =
            (has_space && !ctx_info.add_closing_quote)
                ? ("\"" + app + "\"")
                : (app + (ctx_info.add_closing_quote ? "\"" : ""));
        item.display_text = app;
        item.signature = "application";
        item.description = "/Applications/" + app;
        add_unique(std::move(item));
      }
      AppendPathCompletions(cwd_, ctx_info.prefix, false,
                            !ctx_info.add_closing_quote,
                            ctx_info.add_closing_quote, results);
    } else if (ctx_info.receiver == "/cd") {
      AppendPathCompletions(cwd_, ctx_info.prefix, true,
                            !ctx_info.add_closing_quote,
                            ctx_info.add_closing_quote, results);
    } else if (ctx_info.receiver == "/ls") {
      AppendPathCompletions(cwd_, ctx_info.prefix, false,
                            !ctx_info.add_closing_quote,
                            ctx_info.add_closing_quote, results);
    } else if (ctx_info.receiver == "/kill" || ctx_info.receiver == "/ps") {
      perception::ForEachProcess([&](perception::ProcessId pid) {
        std::string pname = perception::GetProcessName(pid);
        if (pname.empty() ||
            !StartsWithCaseInsensitive(pname, ctx_info.prefix))
          return;
        CompletionItem item;
        bool has_space = (pname.find(' ') != std::string::npos);
        item.insert_text =
            (has_space && !ctx_info.add_closing_quote) ? ("\"" + pname + "\"")
                                                       : pname;
        item.display_text = pname;
        item.signature = "PID " + std::to_string(pid);
        item.description = "Running process";
        add_unique(std::move(item));
      });
    }
    return results;
  }

  if (ctx_info.kind == CompletionKind::kStringTarget) {
    for (const auto& app : module::DiscoverInstalledApplications()) {
      if (!StartsWithCaseInsensitive(app, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = app + (ctx_info.add_closing_quote ? "\"" : "");
      item.display_text = app;
      item.signature = "application";
      item.description = "/Applications/" + app;
      add_unique(std::move(item));
    }
    AppendPathCompletions(cwd_, ctx_info.prefix, false, false,
                          ctx_info.add_closing_quote, results);
    return results;
  }

  if (ctx_info.kind == CompletionKind::kStringPath) {
    AppendPathCompletions(cwd_, ctx_info.prefix, false, false,
                          ctx_info.add_closing_quote, results);
    return results;
  }

  if (ctx_info.kind == CompletionKind::kCommandMethod) {
    for (const auto& entry : GetCommandMethodCatalog()) {
      if (!StartsWithCaseInsensitive(entry.name, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = entry.name + (entry.is_function ? "(" : "");
      item.display_text = entry.name;
      item.signature = entry.signature;
      item.description = entry.description;
      add_unique(std::move(item));
    }
    return results;
  }

  if (ctx_info.kind == CompletionKind::kProperty) {
    for (const auto& entry : GetNamespaceCatalog(ctx_info.receiver)) {
      if (!StartsWithCaseInsensitive(entry.name, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = entry.name + (entry.is_function ? "(" : "");
      item.display_text = entry.name;
      item.signature = entry.signature;
      item.description = entry.description;
      add_unique(std::move(item));
    }

    if (ctx_ != nullptr) {
      JSValue cur = JS_GetGlobalObject(ctx_);
      size_t start = 0;
      bool valid = true;
      while (start < ctx_info.receiver.size()) {
        size_t dot = ctx_info.receiver.find('.', start);
        std::string seg =
            (dot == std::string::npos)
                ? ctx_info.receiver.substr(start)
                : ctx_info.receiver.substr(start, dot - start);
        if (!JS_IsObject(cur)) {
          valid = false;
          break;
        }
        JSValue next = JS_GetPropertyStr(ctx_, cur, seg.c_str());
        JS_FreeValue(ctx_, cur);
        cur = next;
        if (JS_IsUndefined(cur) || JS_IsNull(cur) || JS_IsException(cur)) {
          valid = false;
          break;
        }
        if (dot == std::string::npos) break;
        start = dot + 1;
      }

      if (valid && JS_IsObject(cur)) {
        JSValue proto = JS_DupValue(ctx_, cur);
        for (int depth = 0;
             depth < kMaxPrototypeDepth && JS_IsObject(proto); ++depth) {
          JSPropertyEnum* tab = nullptr;
          uint32_t len = 0;
          if (JS_GetOwnPropertyNames(ctx_, &tab, &len, proto,
                                     JS_GPN_STRING_MASK) == 0) {
            for (uint32_t i = 0; i < len; ++i) {
              const char* prop_name = JS_AtomToCString(ctx_, tab[i].atom);
              if (prop_name != nullptr) {
                std::string pname(prop_name);
                JS_FreeCString(ctx_, prop_name);
                if (!pname.empty() && pname != "__proto__" &&
                    StartsWithCaseInsensitive(pname, ctx_info.prefix)) {
                  JSValue pval = JS_GetProperty(ctx_, cur, tab[i].atom);
                  bool is_fn = JS_IsFunction(ctx_, pval);
                  std::string val_preview =
                      is_fn ? "function" : FormatJsValuePreview(pval, false, 1);
                  JS_FreeValue(ctx_, pval);

                  CompletionItem item;
                  item.insert_text = pname + (is_fn ? "(" : "");
                  item.display_text = pname;
                  item.signature = is_fn ? (pname + "(...)") : val_preview;
                  item.description = "Live property on " + ctx_info.receiver;
                  add_unique(std::move(item));
                }
              }
            }
            JS_FreePropertyEnum(ctx_, tab, len);
          }
          JSValue next_proto = JS_GetPrototype(ctx_, proto);
          JS_FreeValue(ctx_, proto);
          proto = next_proto;
        }
        JS_FreeValue(ctx_, proto);
      }
      JS_FreeValue(ctx_, cur);
    }
    return results;
  }

  if (ctx_info.kind == CompletionKind::kIdentifier) {
    for (const auto& entry : GetGlobalCatalog()) {
      if (!StartsWithCaseInsensitive(entry.name, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = entry.name + (entry.is_function ? "(" : "");
      item.display_text = entry.name;
      item.signature = entry.signature;
      item.description = entry.description;
      add_unique(std::move(item));
    }

    if (ctx_ != nullptr) {
      JSValue global_obj = JS_GetGlobalObject(ctx_);
      JSPropertyEnum* tab = nullptr;
      uint32_t len = 0;
      if (JS_GetOwnPropertyNames(
              ctx_, &tab, &len, global_obj,
              JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
        for (uint32_t i = 0; i < len; ++i) {
          const char* prop_name = JS_AtomToCString(ctx_, tab[i].atom);
          if (prop_name != nullptr) {
            std::string pname(prop_name);
            JS_FreeCString(ctx_, prop_name);
            if (StartsWithCaseInsensitive(pname, ctx_info.prefix)) {
              JSValue pval = JS_GetProperty(ctx_, global_obj, tab[i].atom);
              bool is_fn = JS_IsFunction(ctx_, pval);
              std::string preview = FormatJsValuePreview(pval, false, 1);
              JS_FreeValue(ctx_, pval);

              CompletionItem item;
              item.insert_text = pname + (is_fn ? "(" : "");
              item.display_text = pname;
              item.signature = preview;
              item.description = "Global variable";
              add_unique(std::move(item));
            }
          }
        }
        JS_FreePropertyEnum(ctx_, tab, len);
      }
      JS_FreeValue(ctx_, global_obj);
    }

    for (std::string_view kw : kCompletionKeywords) {
      if (!StartsWithCaseInsensitive(kw, ctx_info.prefix)) continue;
      CompletionItem item;
      item.insert_text = std::string(kw);
      item.display_text = std::string(kw);
      item.signature = "keyword";
      item.description = "JavaScript keyword";
      add_unique(std::move(item));
    }
  }

  return results;
}

void JsEngine::RestoreTerminalState() {
  if (script_alt_screen_) {
    std::cout << "\x1b[?7h\x1b[?1049l";
    script_alt_screen_ = false;
  }
  if (!script_cursor_visible_) {
    std::cout << "\x1b[?25h";
    script_cursor_visible_ = true;
  }
  if (script_raw_mode_) {
    struct termios t = {};
    if (tcgetattr(STDIN_FILENO, &t) == 0) {
      t.c_lflag |= static_cast<tcflag_t>(ICANON | ECHO | ISIG);
      tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
    script_raw_mode_ = false;
  }
  std::cout << "\x1b[?2026l\x1b[?1003l\x1b[?1006l\x1b[?1016l\x1b]22;\x07\x1b[0m"
            << std::flush;
}

bool JsEngine::SetCwd(std::string_view path, std::string& error_out) {
  std::string normalized = NormalizeLexicalPath(cwd_, path);
  auto status_or_stats =
      perception::GetService<perception::StorageManager>().GetFileStatistics(
          perception::RequestWithFilePath(normalized));
  if (!status_or_stats.Ok() || !status_or_stats->exists) {
    error_out = "Directory does not exist: " + normalized;
    return false;
  }
  if (status_or_stats->type != perception::DirectoryEntry::Type::DIRECTORY) {
    error_out = "Not a directory: " + normalized;
    return false;
  }
  cwd_ = normalized;
  return true;
}

size_t JsEngine::RecordQueryBlock(std::string_view text, size_t turn_index) {
  HistoryBlock block;
  block.kind = HistoryBlockKind::kQuery;
  block.turn_index = turn_index;
  block.text = std::string(text);
  block.rendered_lines = SplitLinesPreservingContent(text);
  int row = -1;
  int col = -1;
  if (QueryCursorPosition(row, col) && row > 1) {
    int line_count = std::max(1, static_cast<int>(block.rendered_lines.size()));
    block.end_screen_row = row - 1;
    block.start_screen_row = std::max(1, block.end_screen_row - line_count + 1);
  }
  history_.push_back(std::move(block));
  return history_.size() - 1;
}

void JsEngine::NotifyTerminalScrolled(int lines_scrolled, int screen_rows) {
  if (lines_scrolled <= 0) return;
  (void)screen_rows;
  for (auto& block : history_) {
    if (block.start_screen_row > 0) {
      block.start_screen_row -= lines_scrolled;
      block.end_screen_row -= lines_scrolled;
      if (block.end_screen_row < 1) {
        block.start_screen_row = -1;
        block.end_screen_row = -1;
      } else if (block.start_screen_row < 1) {
        block.start_screen_row = 1;
      }
    }
  }
}

void JsEngine::ClearHistoryScreenCoordinates() {
  for (auto& block : history_) {
    block.start_screen_row = -1;
    block.end_screen_row = -1;
  }
}

bool JsEngine::QueryCursorPosition(int& row, int& col) {
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;

  struct termios orig_termios = {};
  bool had_termios = (tcgetattr(STDIN_FILENO, &orig_termios) == 0);
  if (had_termios) {
    struct termios raw = orig_termios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
  }

  std::cout << "\x1b[6n" << std::flush;

  std::string resp;
  bool parsed = false;
  for (int attempt = 0; attempt < 4; ++attempt) {
    struct pollfd pfd = {};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, kCursorQueryTimeoutMs) <= 0) break;

    char buf[64];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) break;
    resp.append(buf, static_cast<size_t>(n));

    size_t esc = resp.rfind("\x1b[");
    size_t r_pos = (esc != std::string::npos) ? resp.find('R', esc)
                                              : std::string::npos;
    if (esc != std::string::npos && r_pos != std::string::npos) {
      std::string body = resp.substr(esc + 2, r_pos - (esc + 2));
      int r = 0;
      int c = 0;
      if (std::sscanf(body.c_str(), "%d;%d", &r, &c) == 2 && r > 0 && c > 0) {
        row = r;
        col = c;
        parsed = true;
      }
      break;
    }
  }

  if (had_termios) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
  return parsed;
}

void JsEngine::QueryTerminalSize(int& cols, int& rows) {
  struct winsize wsz = {};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &wsz) == 0 && wsz.ws_col > 0 &&
      wsz.ws_row > 0) {
    cols = wsz.ws_col;
    rows = wsz.ws_row;
  } else {
    cols = kDefaultTerminalCols;
    rows = kDefaultTerminalRows;
  }
}

size_t JsEngine::AddBackgroundJob(perception::ProcessId pid,
                                  std::string_view name,
                                  std::string_view command_line) {
  BackgroundJob job;
  job.job_id = next_job_id_++;
  job.pid = pid;
  job.name = std::string(name);
  job.command_line = std::string(command_line);
  job.running = perception::DoesProcessExist(pid);
  jobs_.push_back(std::move(job));
  return jobs_.back().job_id;
}

const std::vector<BackgroundJob>& JsEngine::GetBackgroundJobs() {
  for (auto& job : jobs_) {
    if (job.running) job.running = perception::DoesProcessExist(job.pid);
  }
  return jobs_;
}

JsEngine* GetJsEngine(JSContext* ctx) {
  if (ctx == nullptr) return nullptr;
  return static_cast<JsEngine*>(JS_GetContextOpaque(ctx));
}
