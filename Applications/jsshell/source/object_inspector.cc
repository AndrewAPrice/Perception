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

#include "object_inspector.h"

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "perception/clipboard.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/serialization/value.h"

namespace {

// Poll timeout in milliseconds for interactive TUI event loops.
constexpr int kTuiPollTimeoutMs = 150;

// Maximum properties loaded per object node in the inspector tree.
constexpr uint32_t kMaxPropertiesPerNode = 256;

// Maximum character length of a single-line value summary in the tree view.
constexpr size_t kMaxSummaryChars = 64;

// Number of CPU core percentage slots passed to GetProcessHealthMetrics.
constexpr size_t kMaxCpuCoreSlots = 8;

// ANSI escape sequence to reset SGR attributes.
constexpr std::string_view kAnsiReset = "\x1b[0m";

// Built-in global property names excluded from the User Globals category.
constexpr std::string_view kBuiltinGlobalNames[] = {
    "Object",        "Function",          "Error",          "EvalError",
    "RangeError",    "ReferenceError",    "SyntaxError",    "TypeError",
    "URIError",      "InternalError",     "AggregateError", "Number",
    "BigInt",        "Math",              "Date",           "String",
    "RegExp",        "Array",             "Int8Array",      "Uint8Array",
    "Uint8ClampedArray", "Int16Array",    "Uint16Array",    "Int32Array",
    "Uint32Array",   "BigInt64Array",     "BigUint64Array", "Float32Array",
    "Float64Array",  "ArrayBuffer",       "SharedArrayBuffer", "DataView",
    "JSON",          "Promise",           "Symbol",         "Map",
    "Set",           "WeakMap",           "WeakSet",        "WeakRef",
    "FinalizationRegistry", "Proxy",      "Reflect",        "globalThis",
    "NaN",           "Infinity",          "undefined",      "parseInt",
    "parseFloat",    "isNaN",             "isFinite",       "decodeURI",
    "decodeURIComponent", "encodeURI",    "encodeURIComponent", "escape",
    "unescape",      "eval",              "run",            "pipe",
    "fs",            "proc",              "sys",            "registry",
    "clipboard",     "net",               "term",           "fetch",
    "sleep",         "print"};

// Built-in jsshell namespace names listed in the Memory Explorer.
constexpr std::string_view kExplorerBuiltinNamespaces[] = {
    "fs", "proc", "pipe", "sys", "registry", "clipboard", "net", "term"};

class ScopedAltScreenTui {
 public:
  ScopedAltScreenTui() {
    has_termios_ = (tcgetattr(STDIN_FILENO, &orig_termios_) == 0);
    if (has_termios_) {
      struct termios raw = orig_termios_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    std::cout << "\x1b[?1049h\x1b[?7l\x1b[?25l\x1b[>1u\x1b[?1003h\x1b[?1006h"
              << std::flush;
  }

  ~ScopedAltScreenTui() { Restore(); }

  void Restore() {
    if (restored_) return;
    restored_ = true;
    std::cout << "\x1b[<1u\x1b[?1003l\x1b[?1006l\x1b]22;\x07\x1b[?7h\x1b[?25h"
                 "\x1b[0m\x1b[?1049l"
              << std::flush;
    if (has_termios_) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios_);
  }

 private:
  struct termios orig_termios_ = {};
  bool has_termios_ = false;
  bool restored_ = false;
};

struct InspectorNode {
  std::string key;
  std::string path;
  int depth = 0;
  JSValue js_value = JS_UNDEFINED;
  std::string type_badge;
  std::string summary;
  bool expandable = false;
  bool expanded = false;
  bool children_loaded = false;
  InspectorNode* parent = nullptr;
  std::vector<std::unique_ptr<InspectorNode>> children;

  void FreeTree(JSContext* ctx) {
    for (auto& child : children) {
      if (child) child->FreeTree(ctx);
    }
    children.clear();
    if (ctx != nullptr) {
      JS_FreeValue(ctx, js_value);
      js_value = JS_UNDEFINED;
    }
  }
};

std::string SanitizeSingleLine(std::string_view input, size_t max_chars) {
  std::string out;
  out.reserve(std::min(input.size(), max_chars));
  for (char ch : input) {
    if (ch == '\n' || ch == '\r' || ch == '\t') {
      out.push_back(' ');
    } else if (static_cast<unsigned char>(ch) >= 32) {
      out.push_back(ch);
    }
    if (out.size() >= max_chars) {
      out += "...";
      break;
    }
  }
  return out;
}

std::string FormatByteSize(int64_t bytes) {
  if (bytes < 0) bytes = 0;
  char buf[64];
  if (bytes < 1024) {
    std::snprintf(buf, sizeof(buf), "%lld B", static_cast<long long>(bytes));
  } else if (bytes < 1024 * 1024) {
    std::snprintf(buf, sizeof(buf), "%.1f KB",
                  static_cast<double>(bytes) / 1024.0);
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f MB",
                  static_cast<double>(bytes) / (1024.0 * 1024.0));
  }
  return buf;
}

bool ContainsCaseInsensitive(std::string_view haystack,
                             std::string_view needle) {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;
  for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
    bool match = true;
    for (size_t j = 0; j < needle.size(); ++j) {
      if (std::tolower(static_cast<unsigned char>(haystack[i + j])) !=
          std::tolower(static_cast<unsigned char>(needle[j]))) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void DescribeJsValue(JSContext* ctx, JSValueConst val, std::string& type_badge,
                     std::string& summary, bool& expandable) {
  expandable = false;
  if (JS_IsUndefined(val)) {
    type_badge = "undefined";
    summary = "undefined";
    return;
  }
  if (JS_IsNull(val)) {
    type_badge = "null";
    summary = "null";
    return;
  }
  if (JS_IsBool(val)) {
    type_badge = "boolean";
    summary = JS_ToBool(ctx, val) ? "true" : "false";
    return;
  }
  if (JS_IsNumber(val) || JS_IsBigInt(ctx, val)) {
    type_badge = JS_IsBigInt(ctx, val) ? "bigint" : "number";
    const char* cstr = JS_ToCString(ctx, val);
    summary = (cstr != nullptr) ? cstr : "0";
    if (cstr != nullptr) JS_FreeCString(ctx, cstr);
    return;
  }
  if (JS_IsString(val)) {
    type_badge = "string";
    const char* cstr = JS_ToCString(ctx, val);
    std::string raw = (cstr != nullptr) ? cstr : "";
    if (cstr != nullptr) JS_FreeCString(ctx, cstr);
    summary = "\"" + SanitizeSingleLine(raw, kMaxSummaryChars) + "\"";
    return;
  }
  if (JS_IsFunction(ctx, val)) {
    type_badge = "Function";
    JSValue name_val = JS_GetPropertyStr(ctx, val, "name");
    const char* name_cstr = JS_ToCString(ctx, name_val);
    summary = "ƒ ";
    summary += (name_cstr != nullptr && name_cstr[0] != '\0') ? name_cstr
                                                              : "(anonymous)";
    summary += "()";
    if (name_cstr != nullptr) JS_FreeCString(ctx, name_cstr);
    JS_FreeValue(ctx, name_val);
    return;
  }
  if (JS_IsArray(ctx, val) > 0) {
    expandable = true;
    JSValue len_val = JS_GetPropertyStr(ctx, val, "length");
    uint32_t len = 0;
    JS_ToUint32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    type_badge = "Array(" + std::to_string(len) + ")";

    JSValue json_val =
        JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(json_val) && JS_IsString(json_val)) {
      const char* jc = JS_ToCString(ctx, json_val);
      if (jc != nullptr) {
        summary = SanitizeSingleLine(jc, kMaxSummaryChars);
        JS_FreeCString(ctx, jc);
      }
    } else if (JS_IsException(json_val)) {
      JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_FreeValue(ctx, json_val);
    if (summary.empty()) summary = "[...]";
    return;
  }
  if (JS_IsObject(val)) {
    expandable = true;
    type_badge = "Object";
    JSValue json_val =
        JS_JSONStringify(ctx, val, JS_UNDEFINED, JS_UNDEFINED);
    if (!JS_IsException(json_val) && JS_IsString(json_val)) {
      const char* jc = JS_ToCString(ctx, json_val);
      if (jc != nullptr) {
        summary = SanitizeSingleLine(jc, kMaxSummaryChars);
        JS_FreeCString(ctx, jc);
      }
    } else if (JS_IsException(json_val)) {
      JS_FreeValue(ctx, JS_GetException(ctx));
    }
    JS_FreeValue(ctx, json_val);
    if (summary.empty()) summary = "{...}";
    return;
  }
  type_badge = "value";
  summary = "[value]";
}

void PopulateNodeChildren(JSContext* ctx, InspectorNode& node) {
  if (node.children_loaded || !node.expandable || !JS_IsObject(node.js_value))
    return;
  node.children_loaded = true;

  JSPropertyEnum* tab = nullptr;
  uint32_t len = 0;
  if (JS_GetOwnPropertyNames(ctx, &tab, &len, node.js_value,
                             JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) != 0) {
    return;
  }

  uint32_t limit = std::min(len, kMaxPropertiesPerNode);
  for (uint32_t i = 0; i < limit; ++i) {
    const char* key_cstr = JS_AtomToCString(ctx, tab[i].atom);
    if (key_cstr == nullptr) continue;
    std::string key_str(key_cstr);
    JS_FreeCString(ctx, key_cstr);

    JSValue child_val = JS_GetProperty(ctx, node.js_value, tab[i].atom);
    if (JS_IsException(child_val)) {
      JS_FreeValue(ctx, JS_GetException(ctx));
      child_val = JS_UNDEFINED;
    }

    auto child = std::make_unique<InspectorNode>();
    child->key = key_str;
    bool is_index = !key_str.empty() &&
                    std::all_of(key_str.begin(), key_str.end(), [](char c) {
                      return std::isdigit(static_cast<unsigned char>(c)) != 0;
                    });
    child->path = is_index ? (node.path + "[" + key_str + "]")
                           : (node.path + "." + key_str);
    child->depth = node.depth + 1;
    child->js_value = child_val;
    child->parent = &node;
    DescribeJsValue(ctx, child_val, child->type_badge, child->summary,
                    child->expandable);
    node.children.push_back(std::move(child));
  }

  JS_FreePropertyEnum(ctx, tab, len);
}

void FlattenVisibleNodes(InspectorNode& node, std::string_view filter,
                         std::vector<InspectorNode*>& out) {
  bool matches =
      filter.empty() || ContainsCaseInsensitive(node.key, filter) ||
      ContainsCaseInsensitive(node.summary, filter) ||
      ContainsCaseInsensitive(node.path, filter);
  if (matches) out.push_back(&node);
  if (node.expanded) {
    for (auto& child : node.children) {
      if (child) FlattenVisibleNodes(*child, filter, out);
    }
  }
}

bool IsBuiltinGlobalProperty(std::string_view name) {
  for (std::string_view b : kBuiltinGlobalNames) {
    if (name == b) return true;
  }
  return false;
}

}  // namespace

void RunObjectInspector(JsEngine& engine, JSValue root_value,
                        std::string_view title) {
  JSContext* ctx = engine.Context();
  if (ctx == nullptr) return;

  ScopedAltScreenTui alt_tui;

  InspectorNode root;
  root.key = std::string(title.empty() ? "root" : title);
  root.path = "_";
  root.depth = 0;
  root.js_value = JS_DupValue(ctx, root_value);
  DescribeJsValue(ctx, root.js_value, root.type_badge, root.summary,
                  root.expandable);
  if (root.expandable) {
    PopulateNodeChildren(ctx, root);
    root.expanded = true;
  }

  int selected_idx = 0;
  int scroll_offset = 0;
  bool filter_input_mode = false;
  bool save_var_input_mode = false;
  std::string filter_query;
  std::string save_var_name;
  std::string status_banner;

  auto render = [&]() {
    int cols = 80;
    int rows = 25;
    engine.QueryTerminalSize(cols, rows);

    std::vector<InspectorNode*> visible;
    FlattenVisibleNodes(root, filter_query, visible);
    if (visible.empty()) visible.push_back(&root);
    selected_idx = std::clamp(selected_idx, 0,
                              static_cast<int>(visible.size()) - 1);

    int list_top = 3;
    int list_bottom = std::max(list_top + 1, rows - 3);
    int list_height = list_bottom - list_top + 1;
    if (selected_idx < scroll_offset) scroll_offset = selected_idx;
    if (selected_idx >= scroll_offset + list_height)
      scroll_offset = selected_idx - list_height + 1;
    scroll_offset = std::max(0, scroll_offset);

    std::cout << "\x1b[?2026h\x1b[48;2;17;17;27m";
    for (int r = 1; r <= rows; ++r) {
      std::cout << "\x1b[" << r << ";1H\x1b[2K";
    }

    std::cout << "\x1b[1;1H\x1b[48;2;30;30;46;1;38;2;137;180;250m\x1b[2K"
              << " ❖ OBJECT & JSON INSPECTOR \x1b[0;48;2;30;30;46;38;2;166;173;200m│ "
              << title << " (" << visible.size() << " nodes)";
    if (!filter_query.empty()) {
      std::cout << " │ \x1b[38;2;249;226;175mFilter: \"" << filter_query
                << "\"";
    }
    std::cout << kAnsiReset;

    const InspectorNode* sel_node =
        visible[static_cast<size_t>(selected_idx)];
    std::cout << "\x1b[2;1H\x1b[48;2;24;24;37;38;2;148;226;213m\x1b[2K"
              << " Path: \x1b[1m" << sel_node->path
              << "\x1b[0;48;2;24;24;37;38;2;166;173;200m  Type: "
              << sel_node->type_badge << kAnsiReset;

    for (int i = 0; i < list_height; ++i) {
      int v_idx = scroll_offset + i;
      if (v_idx >= static_cast<int>(visible.size())) break;
      const InspectorNode* node = visible[static_cast<size_t>(v_idx)];
      bool is_sel = (v_idx == selected_idx);
      int row = list_top + i;

      std::cout << "\x1b[" << row << ";1H";
      if (is_sel) {
        std::cout << "\x1b[48;2;49;50;68m\x1b[2K";
      } else {
        std::cout << "\x1b[48;2;17;17;27m\x1b[2K";
      }

      std::string indent(static_cast<size_t>(node->depth * 2), ' ');
      std::string_view glyph =
          node->expandable ? (node->expanded ? "▾ " : "▸ ") : "  ";

      std::cout << " " << indent
                << (is_sel ? "\x1b[1;38;2;249;226;175m"
                           : "\x1b[38;2;137;180;250m")
                << glyph << node->key << "\x1b[38;2;108;112;134m: "
                << "\x1b[38;2;203;166;247m[" << node->type_badge << "] ";

      if (JS_IsString(node->js_value)) {
        std::cout << "\x1b[38;2;166;227;161m" << node->summary;
      } else if (JS_IsNumber(node->js_value) || JS_IsBool(node->js_value)) {
        std::cout << "\x1b[38;2;249;226;175m" << node->summary;
      } else {
        std::cout << "\x1b[38;2;205;214;244m" << node->summary;
      }
      std::cout << kAnsiReset;
    }

    std::cout << "\x1b[" << (rows - 1)
              << ";1H\x1b[48;2;24;24;37m\x1b[2K ";
    if (filter_input_mode) {
      std::cout << "\x1b[1;38;2;249;226;175mFilter / Search: \x1b[0;38;2;205;214;244m"
                << filter_query << "█  \x1b[38;2;108;112;134m(Enter to apply, Esc to clear)";
    } else if (save_var_input_mode) {
      std::cout << "\x1b[1;38;2;166;227;161mSave selected value to globalThis.\x1b[0;38;2;205;214;244m"
                << save_var_name << "█  \x1b[38;2;108;112;134m(Enter to save, Esc to cancel)";
    } else if (!status_banner.empty()) {
      std::cout << "\x1b[1;38;2;166;227;161m" << status_banner;
    }
    std::cout << kAnsiReset;

    std::cout << "\x1b[" << rows << ";1H\x1b[48;2;30;30;46m\x1b[2K "
              << "\x1b[1;38;2;166;227;161m[↑/↓]\x1b[38;2;205;214;244m Navigate  "
              << "\x1b[1;38;2;249;226;175m[Enter/→/←]\x1b[38;2;205;214;244m Expand/Collapse  "
              << "\x1b[1;38;2;137;180;250m[/]\x1b[38;2;205;214;244m Filter  "
              << "\x1b[1;38;2;148;226;213m[c]\x1b[38;2;205;214;244m Copy JSON  "
              << "\x1b[1;38;2;203;166;247m[v]\x1b[38;2;205;214;244m Save Var  "
              << "\x1b[1;38;2;243;139;168m[q/Esc]\x1b[38;2;205;214;244m Close"
              << kAnsiReset << "\x1b[?2026l" << std::flush;
  };

  render();

  bool running = true;
  while (running) {
    struct pollfd pfd = {};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, kTuiPollTimeoutMs) <= 0) continue;

    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) break;

    std::vector<InspectorNode*> visible;
    FlattenVisibleNodes(root, filter_query, visible);
    if (visible.empty()) visible.push_back(&root);
    selected_idx = std::clamp(selected_idx, 0,
                              static_cast<int>(visible.size()) - 1);

    std::string_view raw(buf, static_cast<size_t>(n));
    size_t i = 0;
    while (i < raw.size()) {
      auto ch = static_cast<unsigned char>(raw[i]);

      if (filter_input_mode || save_var_input_mode) {
        std::string& target_str =
            filter_input_mode ? filter_query : save_var_name;
        if (ch == 0x1b) {
          if (i + 2 < raw.size() && raw[i + 1] == '[') {
            size_t end = i + 2;
            while (end < raw.size() &&
                   (static_cast<unsigned char>(raw[end]) < 0x40 ||
                    static_cast<unsigned char>(raw[end]) > 0x7E)) {
              ++end;
            }
            i = (end < raw.size()) ? (end + 1) : raw.size();
          } else {
            ++i;
          }
          if (filter_input_mode) filter_query.clear();
          filter_input_mode = false;
          save_var_input_mode = false;
          render();
          continue;
        }
        ++i;
        if (ch == '\r' || ch == '\n') {
          if (save_var_input_mode && !save_var_name.empty()) {
            InspectorNode* sel = visible[static_cast<size_t>(selected_idx)];
            JSValue global_obj = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global_obj, save_var_name.c_str(),
                              JS_DupValue(ctx, sel->js_value));
            JS_FreeValue(ctx, global_obj);
            status_banner = "✓ Saved selected node to globalThis." +
                            save_var_name;
          }
          filter_input_mode = false;
          save_var_input_mode = false;
          render();
          continue;
        }
        if (ch == 0x7f || ch == '\b') {
          if (!target_str.empty()) target_str.pop_back();
          render();
          continue;
        }
        if (ch >= 32 && ch < 127) {
          target_str.push_back(static_cast<char>(ch));
          selected_idx = 0;
          render();
        }
        continue;
      }

      if (ch == 0x1b) {
        if (i + 1 >= raw.size()) {
          running = false;
          break;
        }
        if (raw[i + 1] == '[') {
          size_t seq_start = i + 2;
          size_t seq_end = seq_start;
          while (seq_end < raw.size() &&
                 (static_cast<unsigned char>(raw[seq_end]) < 0x40 ||
                  static_cast<unsigned char>(raw[seq_end]) > 0x7E)) {
            ++seq_end;
          }
          if (seq_end >= raw.size()) break;
          char final_ch = raw[seq_end];
          std::string_view body = raw.substr(seq_start, seq_end - seq_start);
          i = seq_end + 1;

          if (body.starts_with("<") && (final_ch == 'M' || final_ch == 'm')) {
            int btn = 0;
            int mcol = 1;
            int mrow = 1;
            std::string bstr(body.substr(1));
            if (std::sscanf(bstr.c_str(), "%d;%d;%d", &btn, &mcol, &mrow) ==
                3) {
              if (btn == 64 && selected_idx > 0) {
                --selected_idx;
                render();
              } else if (btn == 65 &&
                         selected_idx + 1 < static_cast<int>(visible.size())) {
                ++selected_idx;
                render();
              } else if (final_ch == 'M' && (btn & 32) == 0 && (btn & 3) == 0 &&
                         mrow >= 3) {
                int clicked = scroll_offset + (mrow - 3);
                if (clicked >= 0 &&
                    clicked < static_cast<int>(visible.size())) {
                  selected_idx = clicked;
                  InspectorNode* sel =
                      visible[static_cast<size_t>(selected_idx)];
                  if (sel->expandable) {
                    if (!sel->children_loaded) PopulateNodeChildren(ctx, *sel);
                    sel->expanded = !sel->expanded;
                  }
                  render();
                }
              }
            }
            continue;
          }

          if (final_ch == 'u') {
            int cp = 0;
            int mods = 1;
            std::string bstr(body);
            std::sscanf(bstr.c_str(), "%d;%d", &cp, &mods);
            if (cp == 27) {
              running = false;
              break;
            }
            if ((cp == 'm' || cp == 'M') && (mods & 4) != 0) {
              alt_tui.Restore();
              RunMemoryExplorer(engine);
              root.FreeTree(ctx);
              return;
            }
            if (cp == 13) {
              ch = '\r';
            } else if (cp >= 32 && cp < 127) {
              ch = static_cast<unsigned char>(cp);
            } else {
              continue;
            }
          } else if (final_ch == 'A') {
            if (selected_idx > 0) --selected_idx;
            render();
            continue;
          } else if (final_ch == 'B') {
            if (selected_idx + 1 < static_cast<int>(visible.size()))
              ++selected_idx;
            render();
            continue;
          } else if (final_ch == 'C') {
            InspectorNode* sel = visible[static_cast<size_t>(selected_idx)];
            if (sel->expandable) {
              if (!sel->children_loaded) PopulateNodeChildren(ctx, *sel);
              sel->expanded = true;
              render();
            }
            continue;
          } else if (final_ch == 'D') {
            InspectorNode* sel = visible[static_cast<size_t>(selected_idx)];
            if (sel->expanded) {
              sel->expanded = false;
              render();
            } else if (sel->parent != nullptr) {
              for (size_t k = 0; k < visible.size(); ++k) {
                if (visible[k] == sel->parent) {
                  selected_idx = static_cast<int>(k);
                  break;
                }
              }
              render();
            }
            continue;
          } else if (final_ch == '~') {
            if (body.starts_with("5"))
              selected_idx = std::max(0, selected_idx - 10);
            if (body.starts_with("6"))
              selected_idx = std::min(static_cast<int>(visible.size()) - 1,
                                      selected_idx + 10);
            render();
            continue;
          } else {
            continue;
          }
        } else {
          ++i;
          continue;
        }
      } else {
        ++i;
      }

      if (ch == 'q' || ch == 'Q' || ch == 3) {
        running = false;
        break;
      }
      if (ch == 'j') {
        if (selected_idx + 1 < static_cast<int>(visible.size())) ++selected_idx;
        render();
        continue;
      }
      if (ch == 'k') {
        if (selected_idx > 0) --selected_idx;
        render();
        continue;
      }
      if (ch == '\r' || ch == '\n' || ch == ' ') {
        InspectorNode* sel = visible[static_cast<size_t>(selected_idx)];
        if (sel->expandable) {
          if (!sel->children_loaded) PopulateNodeChildren(ctx, *sel);
          sel->expanded = !sel->expanded;
          render();
        }
        continue;
      }
      if (ch == '/') {
        filter_input_mode = true;
        status_banner.clear();
        render();
        continue;
      }
      if (ch == 'v' || ch == 'V') {
        save_var_input_mode = true;
        save_var_name = "_selected";
        status_banner.clear();
        render();
        continue;
      }
      if (ch == 'c' || ch == 'C') {
        InspectorNode* sel = visible[static_cast<size_t>(selected_idx)];
        JSValue indent = JS_NewInt32(ctx, 2);
        JSValue json_val =
            JS_JSONStringify(ctx, sel->js_value, JS_UNDEFINED, indent);
        JS_FreeValue(ctx, indent);

        std::string copied_text;
        if (!JS_IsException(json_val) && JS_IsString(json_val)) {
          const char* jc = JS_ToCString(ctx, json_val);
          if (jc != nullptr) {
            copied_text = jc;
            JS_FreeCString(ctx, jc);
          }
        } else if (JS_IsException(json_val)) {
          JS_FreeValue(ctx, JS_GetException(ctx));
        }
        JS_FreeValue(ctx, json_val);

        if (copied_text.empty()) copied_text = sel->summary;
        perception::SetClipboard(perception::serialization::Value(copied_text));
        status_banner = "✓ Copied " + std::to_string(copied_text.size()) +
                        " bytes to system Clipboard";
        render();
      }
    }
  }

  root.FreeTree(ctx);
}

void RunMemoryExplorer(JsEngine& engine) {
  JSContext* ctx = engine.Context();
  JSRuntime* rt = engine.Runtime();
  if (ctx == nullptr || rt == nullptr) return;

  ScopedAltScreenTui alt_tui;

  struct MemoryItem {
    std::string category;
    std::string name;
    std::string type_badge;
    std::string summary;
    JSValue js_value = JS_UNDEFINED;
    bool is_user_global = false;
    bool is_history_block = false;
    size_t history_index = 0;
  };

  std::vector<MemoryItem> items;
  int selected_idx = 0;
  int scroll_offset = 0;
  std::string status_banner;

  auto free_items = [&]() {
    for (auto& it : items) {
      JS_FreeValue(ctx, it.js_value);
      it.js_value = JS_UNDEFINED;
    }
    items.clear();
  };

  auto rebuild_items = [&]() {
    free_items();
    JSValue global_obj = JS_GetGlobalObject(ctx);

    JSPropertyEnum* tab = nullptr;
    uint32_t len = 0;
    if (JS_GetOwnPropertyNames(ctx, &tab, &len, global_obj,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
      for (uint32_t i = 0; i < len; ++i) {
        const char* prop_cstr = JS_AtomToCString(ctx, tab[i].atom);
        if (prop_cstr == nullptr) continue;
        std::string pname(prop_cstr);
        JS_FreeCString(ctx, prop_cstr);

        if (IsBuiltinGlobalProperty(pname)) continue;
        JSValue val = JS_GetProperty(ctx, global_obj, tab[i].atom);
        if (pname == "_" && JS_IsUndefined(val)) {
          JS_FreeValue(ctx, val);
          continue;
        }

        MemoryItem item;
        item.category = "USER GLOBAL";
        item.name = pname;
        item.js_value = val;
        item.is_user_global = true;
        bool exp = false;
        DescribeJsValue(ctx, val, item.type_badge, item.summary, exp);
        items.push_back(std::move(item));
      }
      JS_FreePropertyEnum(ctx, tab, len);
    }

    for (size_t h = 0; h < engine.History().size(); ++h) {
      const auto& blk = engine.History()[h];
      if (blk.kind == HistoryBlockKind::kResponse && blk.has_js_value) {
        MemoryItem item;
        item.category = "HISTORY";
        item.name = "Response #" + std::to_string(blk.turn_index);
        item.js_value = JS_DupValue(ctx, blk.js_value);
        item.is_history_block = true;
        item.history_index = h;
        bool exp = false;
        DescribeJsValue(ctx, blk.js_value, item.type_badge, item.summary, exp);
        items.push_back(std::move(item));
      }
    }

    for (std::string_view ns : kExplorerBuiltinNamespaces) {
      JSValue ns_val =
          JS_GetPropertyStr(ctx, global_obj, std::string(ns).c_str());
      if (!JS_IsUndefined(ns_val)) {
        MemoryItem item;
        item.category = "BUILTIN";
        item.name = std::string(ns);
        item.js_value = ns_val;
        bool exp = false;
        DescribeJsValue(ctx, ns_val, item.type_badge, item.summary, exp);
        items.push_back(std::move(item));
      } else {
        JS_FreeValue(ctx, ns_val);
      }
    }

    JS_FreeValue(ctx, global_obj);
    if (!items.empty()) {
      selected_idx =
          std::clamp(selected_idx, 0, static_cast<int>(items.size()) - 1);
    } else {
      selected_idx = 0;
    }
  };

  rebuild_items();

  auto render = [&]() {
    int cols = 80;
    int rows = 25;
    engine.QueryTerminalSize(cols, rows);

    JSMemoryUsage qjs_mem = {};
    JS_ComputeMemoryUsage(rt, &qjs_mem);

    size_t total_sys = perception::GetTotalSystemMemory();
    size_t free_sys = perception::GetFreeSystemMemory();
    size_t used_sys = (total_sys > free_sys) ? (total_sys - free_sys) : 0;

    size_t proc_unique = 0;
    size_t proc_shared = 0;
    size_t proc_created = 0;
    size_t proc_services = 0;
    uint8 cpu_pct[kMaxCpuCoreSlots] = {};
    perception::GetProcessHealthMetrics(0, proc_unique, proc_shared,
                                        proc_created, proc_services, cpu_pct,
                                        kMaxCpuCoreSlots);

    std::cout << "\x1b[?2026h\x1b[48;2;17;17;27m";
    for (int r = 1; r <= rows; ++r) {
      std::cout << "\x1b[" << r << ";1H\x1b[2K";
    }

    std::cout << "\x1b[1;1H\x1b[48;2;30;30;46;1;38;2;249;226;175m\x1b[2K"
              << " ⚡ JSSHELL MEMORY EXPLORER & GC TELEMETRY" << kAnsiReset;

    std::cout << "\x1b[2;1H\x1b[48;2;24;24;37;38;2;137;180;250m\x1b[2K"
              << " QuickJS Heap: \x1b[1;38;2;166;227;161m"
              << FormatByteSize(qjs_mem.memory_used_size)
              << "\x1b[0;48;2;24;24;37;38;2;166;173;200m (malloc: "
              << FormatByteSize(qjs_mem.malloc_size) << ") │ Objects: \x1b[1m"
              << qjs_mem.obj_count << "\x1b[0;48;2;24;24;37;38;2;166;173;200m │ Strings: \x1b[1m"
              << qjs_mem.str_count << "\x1b[0;48;2;24;24;37;38;2;166;173;200m │ Arrays: \x1b[1m"
              << qjs_mem.array_count << kAnsiReset;

    std::cout << "\x1b[3;1H\x1b[48;2;24;24;37;38;2;148;226;213m\x1b[2K"
              << " Process RAM:  \x1b[1m"
              << FormatByteSize(static_cast<int64_t>(proc_unique))
              << " unique\x1b[0;48;2;24;24;37;38;2;166;173;200m + "
              << FormatByteSize(static_cast<int64_t>(proc_shared))
              << " shared │ System RAM: "
              << FormatByteSize(static_cast<int64_t>(used_sys)) << " / "
              << FormatByteSize(static_cast<int64_t>(total_sys)) << kAnsiReset;

    int list_top = 5;
    int list_bottom = std::max(list_top + 1, rows - 3);
    int list_height = list_bottom - list_top + 1;
    if (selected_idx < scroll_offset) scroll_offset = selected_idx;
    if (selected_idx >= scroll_offset + list_height)
      scroll_offset = selected_idx - list_height + 1;
    scroll_offset = std::max(0, scroll_offset);

    for (int i = 0; i < list_height; ++i) {
      int idx = scroll_offset + i;
      if (idx >= static_cast<int>(items.size())) break;
      const auto& item = items[static_cast<size_t>(idx)];
      bool is_sel = (idx == selected_idx);
      int row = list_top + i;

      std::cout << "\x1b[" << row << ";1H"
                << (is_sel ? "\x1b[48;2;49;50;68m\x1b[2K"
                           : "\x1b[48;2;17;17;27m\x1b[2K");

      std::string_view badge_color =
          item.is_user_global
              ? "\x1b[1;38;2;166;227;161m"
              : (item.is_history_block ? "\x1b[1;38;2;249;226;175m"
                                       : "\x1b[1;38;2;137;180;250m");
      std::cout << " " << (is_sel ? "▸ " : "  ") << badge_color << "["
                << item.category << "] \x1b[1;38;2;205;214;244m" << item.name
                << " \x1b[0;38;2;203;166;247m(" << item.type_badge << ") "
                << "\x1b[38;2;166;173;200m" << item.summary << kAnsiReset;
    }

    if (!status_banner.empty()) {
      std::cout << "\x1b[" << (rows - 1)
                << ";1H\x1b[48;2;24;24;37;1;38;2;166;227;161m\x1b[2K "
                << status_banner << kAnsiReset;
    }

    std::cout << "\x1b[" << rows << ";1H\x1b[48;2;30;30;46m\x1b[2K "
              << "\x1b[1;38;2;166;227;161m[↑/↓]\x1b[38;2;205;214;244m Select  "
              << "\x1b[1;38;2;249;226;175m[Enter]\x1b[38;2;205;214;244m Inspect Object  "
              << "\x1b[1;38;2;243;139;168m[d/Del]\x1b[38;2;205;214;244m Free/Delete  "
              << "\x1b[1;38;2;148;226;213m[g]\x1b[38;2;205;214;244m Run GC  "
              << "\x1b[1;38;2;203;166;247m[q/Esc]\x1b[38;2;205;214;244m Close"
              << kAnsiReset << "\x1b[?2026l" << std::flush;
  };

  render();

  bool running = true;
  while (running) {
    struct pollfd pfd = {};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, kTuiPollTimeoutMs) <= 0) continue;

    char buf[256];
    ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
    if (n <= 0) break;

    std::string_view raw(buf, static_cast<size_t>(n));
    size_t i = 0;
    while (i < raw.size()) {
      auto ch = static_cast<unsigned char>(raw[i]);
      if (ch == 0x1b) {
        if (i + 1 >= raw.size()) {
          running = false;
          break;
        }
        if (raw[i + 1] == '[') {
          size_t seq_start = i + 2;
          size_t seq_end = seq_start;
          while (seq_end < raw.size() &&
                 (static_cast<unsigned char>(raw[seq_end]) < 0x40 ||
                  static_cast<unsigned char>(raw[seq_end]) > 0x7E)) {
            ++seq_end;
          }
          if (seq_end >= raw.size()) break;
          char final_ch = raw[seq_end];
          std::string_view body = raw.substr(seq_start, seq_end - seq_start);
          i = seq_end + 1;

          if (body.starts_with("<") && (final_ch == 'M' || final_ch == 'm')) {
            int btn = 0;
            int mcol = 1;
            int mrow = 1;
            std::string bstr(body.substr(1));
            if (std::sscanf(bstr.c_str(), "%d;%d;%d", &btn, &mcol, &mrow) ==
                3) {
              if (btn == 64 && selected_idx > 0) {
                --selected_idx;
                render();
              } else if (btn == 65 &&
                         selected_idx + 1 < static_cast<int>(items.size())) {
                ++selected_idx;
                render();
              } else if (final_ch == 'M' && (btn & 32) == 0 && (btn & 3) == 0 &&
                         mrow >= 5) {
                int clicked = scroll_offset + (mrow - 5);
                if (clicked >= 0 && clicked < static_cast<int>(items.size())) {
                  selected_idx = clicked;
                  render();
                }
              }
            }
            continue;
          }

          if (final_ch == 'u') {
            int cp = 0;
            int mods = 1;
            std::string bstr(body);
            std::sscanf(bstr.c_str(), "%d;%d", &cp, &mods);
            if (cp == 27) {
              running = false;
              break;
            }
            if (cp == 13) {
              ch = '\r';
            } else if (cp == 127) {
              ch = 'd';
            } else if (cp >= 32 && cp < 127) {
              ch = static_cast<unsigned char>(cp);
            } else {
              continue;
            }
          } else if (final_ch == 'A') {
            if (selected_idx > 0) --selected_idx;
            render();
            continue;
          } else if (final_ch == 'B') {
            if (selected_idx + 1 < static_cast<int>(items.size()))
              ++selected_idx;
            render();
            continue;
          } else if (final_ch == '~' && body.starts_with("3")) {
            ch = 'd';
          } else {
            continue;
          }
        } else {
          ++i;
          continue;
        }
      } else {
        ++i;
      }

      if (ch == 'q' || ch == 'Q' || ch == 3) {
        running = false;
        break;
      }
      if (ch == 'j') {
        if (selected_idx + 1 < static_cast<int>(items.size())) ++selected_idx;
        render();
        continue;
      }
      if (ch == 'k') {
        if (selected_idx > 0) --selected_idx;
        render();
        continue;
      }
      if (ch == 'g' || ch == 'G') {
        JSMemoryUsage before = {};
        JSMemoryUsage after = {};
        JS_ComputeMemoryUsage(rt, &before);
        JS_RunGC(rt);
        JS_ComputeMemoryUsage(rt, &after);
        int64_t freed =
            std::max<int64_t>(0, before.memory_used_size - after.memory_used_size);
        status_banner = "✓ Ran JS_RunGC — reclaimed " + FormatByteSize(freed);
        rebuild_items();
        render();
        continue;
      }
      if (ch == 'd' || ch == 'D' || ch == 0x7f) {
        if (!items.empty()) {
          const auto& sel = items[static_cast<size_t>(selected_idx)];
          if (sel.is_user_global) {
            std::string var_name = sel.name;
            JSValue global_obj = JS_GetGlobalObject(ctx);
            if (var_name == "_") {
              JS_SetPropertyStr(ctx, global_obj, "_", JS_UNDEFINED);
            } else {
              JSAtom atom = JS_NewAtom(ctx, var_name.c_str());
              JS_DeleteProperty(ctx, global_obj, atom, 0);
              JS_FreeAtom(ctx, atom);
            }
            JS_FreeValue(ctx, global_obj);
            JS_RunGC(rt);
            status_banner = "✓ Deleted global variable '" + var_name + "'";
            rebuild_items();
            render();
          } else if (sel.is_history_block &&
                     sel.history_index < engine.History().size()) {
            auto& blk = engine.History()[sel.history_index];
            if (blk.has_js_value) {
              JS_FreeValue(ctx, blk.js_value);
              blk.js_value = JS_UNDEFINED;
              blk.has_js_value = false;
            }
            JS_RunGC(rt);
            status_banner = "✓ Released cached history value for " + sel.name;
            rebuild_items();
            render();
          } else {
            status_banner = "Built-in namespaces cannot be deleted.";
            render();
          }
        }
        continue;
      }
      if ((ch == '\r' || ch == '\n') && !items.empty()) {
        JSValue target_val =
            JS_DupValue(ctx, items[static_cast<size_t>(selected_idx)].js_value);
        std::string target_title =
            items[static_cast<size_t>(selected_idx)].name;
        alt_tui.Restore();
        RunObjectInspector(engine, target_val, target_title);
        JS_FreeValue(ctx, target_val);
        return;
      }
    }
  }

  free_items();
}
