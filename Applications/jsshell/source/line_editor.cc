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

#include "line_editor.h"

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "completion.h"
#include "object_inspector.h"
#include "slash_commands.h"

namespace {

// Maximum number of completion candidates shown in the below-line popup menu.
constexpr size_t kMaxCompletionMenuRows = 6;

// Poll timeout in milliseconds when waiting for keyboard/mouse input.
constexpr int kEditorPollTimeoutMs = 100;

// ASCII control code for Ctrl+A (Home).
constexpr unsigned char kCtrlA = 1;

// ASCII control code for Ctrl+C (Cancel line).
constexpr unsigned char kCtrlC = 3;

// ASCII control code for Ctrl+D (EOF / Delete).
constexpr unsigned char kCtrlD = 4;

// ASCII control code for Ctrl+E (End).
constexpr unsigned char kCtrlE = 5;

// ASCII control code for Ctrl+K (Kill to end of line).
constexpr unsigned char kCtrlK = 11;

// ASCII control code for Ctrl+L (Clear screen).
constexpr unsigned char kCtrlL = 12;

// ASCII control code for Ctrl+U (Kill to start of line).
constexpr unsigned char kCtrlU = 21;

// ASCII control code for Ctrl+W (Delete previous word).
constexpr unsigned char kCtrlW = 23;

// ANSI escape sequence to reset SGR attributes.
constexpr std::string_view kAnsiReset = "\x1b[0m";

// ANSI color for JS keywords.
constexpr std::string_view kColorKeyword = "\x1b[1;38;2;203;166;247m";

// ANSI color for built-in namespaces and functions.
constexpr std::string_view kColorBuiltin = "\x1b[38;2;137;180;250m";

// ANSI color for string literals.
constexpr std::string_view kColorString = "\x1b[38;2;166;227;161m";

// ANSI color for numeric literals.
constexpr std::string_view kColorNumber = "\x1b[38;2;249;226;175m";

// ANSI color for slash commands.
constexpr std::string_view kColorSlash = "\x1b[1;38;2;148;226;213m";

// ANSI color for dim ghost text and comments.
constexpr std::string_view kColorGhost = "\x1b[38;2;108;112;134m";

// JavaScript language keywords highlighted in the line editor.
constexpr std::string_view kEditorKeywords[] = {
    "await",    "async",      "let",      "const",    "var",       "function",
    "return",   "if",         "else",     "for",      "while",     "do",
    "switch",   "case",       "break",    "continue", "try",       "catch",
    "finally",  "throw",      "new",      "typeof",   "instanceof", "in",
    "of",       "true",       "false",    "null",     "undefined", "class",
    "import",   "export",     "from"};

// Built-in jsshell globals and namespaces highlighted in the line editor.
constexpr std::string_view kEditorBuiltins[] = {
    "run",       "pipe",   "fs",      "proc",    "sys",   "registry",
    "clipboard", "net",    "term",    "fetch",   "sleep", "print",
    "_",         "JSON",   "Math",    "Object",  "Array", "Promise"};

class ScopedEditorTerminalMode {
 public:
  ScopedEditorTerminalMode() {
    has_termios_ = (tcgetattr(STDIN_FILENO, &orig_termios_) == 0);
    if (has_termios_) {
      struct termios raw = orig_termios_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    std::cout << "\x1b[>1u\x1b[?1003h\x1b[?1006h" << std::flush;
  }

  ~ScopedEditorTerminalMode() { Restore(); }

  void Restore() {
    if (restored_) return;
    restored_ = true;
    std::cout << "\x1b[<1u\x1b[?1003l\x1b[?1006l\x1b]22;\x07\x1b[0m"
              << std::flush;
    if (has_termios_) tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios_);
  }

 private:
  struct termios orig_termios_ = {};
  bool has_termios_ = false;
  bool restored_ = false;
};

bool IsIdentChar(char ch) {
  auto u = static_cast<unsigned char>(ch);
  return std::isalnum(u) || ch == '_' || ch == '$';
}

bool IsJsKeywordToken(std::string_view tok) {
  for (std::string_view kw : kEditorKeywords) {
    if (tok == kw) return true;
  }
  return false;
}

bool IsBuiltinToken(std::string_view tok) {
  for (std::string_view b : kEditorBuiltins) {
    if (tok == b) return true;
  }
  return false;
}

std::string HighlightInputLine(std::string_view line) {
  if (line.empty()) return "";

  if (IsSlashCommandInput(line)) {
    std::string out;
    size_t i = 0;
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i]))) {
      out.push_back(line[i]);
      ++i;
    }
    size_t cmd_end = i;
    while (cmd_end < line.size() &&
           !std::isspace(static_cast<unsigned char>(line[cmd_end]))) {
      ++cmd_end;
    }
    out += kColorSlash;
    out.append(line.substr(i, cmd_end - i));
    out += kAnsiReset;
    out.append(line.substr(cmd_end));
    return out;
  }

  std::string out;
  size_t i = 0;
  while (i < line.size()) {
    char ch = line[i];
    if (ch == '/' && i + 1 < line.size() && line[i + 1] == '/') {
      out += kColorGhost;
      out.append(line.substr(i));
      out += kAnsiReset;
      break;
    }
    if (ch == '"' || ch == '\'' || ch == '`') {
      char quote = ch;
      size_t end = i + 1;
      while (end < line.size()) {
        if (line[end] == '\\' && end + 1 < line.size()) {
          end += 2;
          continue;
        }
        if (line[end] == quote) {
          ++end;
          break;
        }
        ++end;
      }
      out += kColorString;
      out.append(line.substr(i, end - i));
      out += kAnsiReset;
      i = end;
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(ch))) {
      size_t end = i + 1;
      while (end < line.size() &&
             (std::isalnum(static_cast<unsigned char>(line[end])) ||
              line[end] == '.')) {
        ++end;
      }
      out += kColorNumber;
      out.append(line.substr(i, end - i));
      out += kAnsiReset;
      i = end;
      continue;
    }
    if (IsIdentChar(ch)) {
      size_t end = i + 1;
      while (end < line.size() && IsIdentChar(line[end])) ++end;
      std::string_view tok = line.substr(i, end - i);
      if (IsJsKeywordToken(tok)) {
        out += kColorKeyword;
        out.append(tok);
        out += kAnsiReset;
      } else if (IsBuiltinToken(tok)) {
        out += kColorBuiltin;
        out.append(tok);
        out += kAnsiReset;
      } else {
        out.append(tok);
      }
      i = end;
      continue;
    }
    out.push_back(ch);
    ++i;
  }
  return out;
}

std::string FormatPromptString(std::string_view cwd, bool is_continuation,
                               int& visible_cols_out) {
  if (is_continuation) {
    visible_cols_out = 6;
    return "\x1b[38;2;108;112;134m... ❯ \x1b[0m";
  }
  std::string short_cwd(cwd);
  if (short_cwd.size() > 24)
    short_cwd = "…" + short_cwd.substr(short_cwd.size() - 23);
  visible_cols_out = static_cast<int>(short_cwd.size()) + 3;
  return "\x1b[1;38;2;137;180;250m" + short_cwd +
         " \x1b[1;38;2;166;227;161m❯\x1b[0m ";
}

void DrawBlockOverlayOnScreen(const HistoryBlock& block, int cols,
                              bool highlighted) {
  if (block.start_screen_row < 1 || block.end_screen_row < block.start_screen_row)
    return;
  std::cout << "\0337";
  int max_rows = block.end_screen_row - block.start_screen_row + 1;
  for (int i = 0;
       i < max_rows && i < static_cast<int>(block.rendered_lines.size()); ++i) {
    int r = block.start_screen_row + i;
    std::string line = block.rendered_lines[static_cast<size_t>(i)];
    if (static_cast<int>(line.size()) > cols - 2)
      line.resize(static_cast<size_t>(std::max(1, cols - 2)));
    std::cout << "\x1b[" << r << ";1H\x1b[2K";
    if (highlighted) {
      std::cout << "\x1b[48;2;49;50;68;38;2;249;226;175m";
      if (block.kind == HistoryBlockKind::kQuery && i == 0)
        std::cout << "❯ ";
      std::cout << line << "\x1b[0m";
    } else {
      if (block.kind == HistoryBlockKind::kQuery && i == 0)
        std::cout << "\x1b[1;38;2;166;227;161m❯ \x1b[0m"
                  << HighlightInputLine(line);
      else
        std::cout << line;
    }
  }
  std::cout << "\0338";
}

}  // namespace

LineEditor::LineEditor(JsEngine& engine) : engine_(engine) {}

LineEditor::~LineEditor() = default;

LineEditorResult LineEditor::ReadCommand(size_t turn_index) {
  (void)turn_index;
  ScopedEditorTerminalMode term_mode;

  int cols = 80;
  int rows = 25;
  engine_.QueryTerminalSize(cols, rows);

  std::string accumulated;
  std::string buffer = prefill_buffer_;
  prefill_buffer_.clear();
  size_t cursor = buffer.size();

  bool is_continuation = false;
  int prompt_cols = 0;
  std::string prompt_str =
      FormatPromptString(engine_.Cwd(), is_continuation, prompt_cols);

  int prompt_row = rows;
  int dummy_col = 1;
  if (engine_.QueryCursorPosition(prompt_row, dummy_col) && dummy_col > 1) {
    std::cout << "\n";
    engine_.QueryCursorPosition(prompt_row, dummy_col);
  }

  std::vector<CompletionItem> menu_items;
  int menu_selected = -1;
  size_t menu_replace_start = 0;
  std::string menu_original_prefix;
  int drawn_menu_rows = 0;

  int selected_history_idx = -1;
  int hovered_history_idx = -1;
  std::string saved_live_buffer;

  auto clear_menu_rows = [&]() {
    if (drawn_menu_rows <= 0) return;
    std::cout << "\0337";
    for (int i = 1; i <= drawn_menu_rows; ++i) {
      std::cout << "\x1b[" << (prompt_row + i) << ";1H\x1b[2K";
    }
    std::cout << "\0338";
    drawn_menu_rows = 0;
  };

  auto compute_ghost_suffix = [&]() -> std::string {
    if (cursor != buffer.size() || buffer.empty() ||
        selected_history_idx >= 0 || !menu_items.empty())
      return "";
    CompletionContext ctx = AnalyzeCompletionContext(buffer, cursor);
    if (ctx.kind == CompletionKind::kNone || ctx.prefix.empty()) return "";
    auto candidates = engine_.GetCompletions(buffer, cursor);
    if (candidates.empty()) return "";
    const std::string& best = candidates.front().insert_text;
    if (best.size() <= ctx.prefix.size()) return "";
    for (size_t i = 0; i < ctx.prefix.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(best[i])) !=
          std::tolower(static_cast<unsigned char>(ctx.prefix[i])))
        return "";
    }
    return best.substr(ctx.prefix.size());
  };

  auto redraw_line = [&]() {
    std::cout << "\x1b[?2026h\r\x1b[2K" << prompt_str;

    if (selected_history_idx >= 0 &&
        selected_history_idx < static_cast<int>(engine_.History().size())) {
      const auto& blk =
          engine_.History()[static_cast<size_t>(selected_history_idx)];
      std::string summary = blk.rendered_lines.empty()
                                ? blk.text
                                : blk.rendered_lines.front();
      if (summary.size() > 42) summary = summary.substr(0, 42) + "...";
      if (blk.kind == HistoryBlockKind::kQuery) {
        std::cout << "\x1b[1;48;2;49;50;68;38;2;249;226;175m [Query #"
                  << blk.turn_index << "] \x1b[0;38;2;205;214;244m" << summary
                  << " \x1b[38;2;166;227;161m(Enter: edit, Esc: cancel)\x1b[0m";
      } else {
        std::cout << "\x1b[1;48;2;49;50;68;38;2;137;180;250m [Response #"
                  << blk.turn_index << "] \x1b[0;38;2;205;214;244m" << summary
                  << " \x1b[38;2;249;226;175m(Enter: inspect, Esc: cancel)\x1b[0m";
      }
      std::cout << "\x1b[?2026l" << std::flush;
      return;
    }

    std::cout << HighlightInputLine(buffer);
    std::string ghost = compute_ghost_suffix();
    if (!ghost.empty()) std::cout << kColorGhost << ghost << kAnsiReset;

    if (!menu_items.empty()) {
      int count = static_cast<int>(
          std::min(kMaxCompletionMenuRows, menu_items.size()));
      int needed_space = count;
      int available_below = rows - prompt_row;
      if (available_below < needed_space) {
        int scroll_by = needed_space - available_below;
        for (int s = 0; s < scroll_by; ++s) std::cout << "\n";
        prompt_row -= scroll_by;
        engine_.NotifyTerminalScrolled(scroll_by, rows);
        std::cout << "\x1b[" << prompt_row << ";1H\x1b[2K" << prompt_str
                  << HighlightInputLine(buffer);
      }

      int start_idx = 0;
      if (menu_selected >= count) start_idx = menu_selected - count + 1;

      std::cout << "\0337";
      for (int i = 0; i < count; ++i) {
        size_t idx = static_cast<size_t>(start_idx + i);
        const auto& item = menu_items[idx];
        bool is_sel = (static_cast<int>(idx) == menu_selected);
        std::cout << "\x1b[" << (prompt_row + 1 + i) << ";1H\x1b[2K  ";
        if (is_sel) {
          std::cout << "\x1b[1;48;2;69;71;90;38;2;249;226;175m ▸ "
                    << item.display_text << " \x1b[0;48;2;49;50;68;38;2;148;226;213m "
                    << item.signature << " \x1b[38;2;166;173;200m— "
                    << item.description << " \x1b[0m";
        } else {
          std::cout << "\x1b[38;2;137;180;250m   " << item.display_text
                    << " \x1b[38;2;108;112;134m" << item.signature << " — "
                    << item.description << "\x1b[0m";
        }
      }
      drawn_menu_rows = count;
      std::cout << "\0338";
    }

    int target_col = prompt_cols + static_cast<int>(cursor) + 1;
    std::cout << "\x1b[" << prompt_row << ";" << target_col << "H\x1b[?2026l"
              << std::flush;
  };

  auto set_selected_history = [&](int new_idx) {
    if (selected_history_idx == new_idx) return;
    if (selected_history_idx >= 0 &&
        selected_history_idx < static_cast<int>(engine_.History().size())) {
      DrawBlockOverlayOnScreen(
          engine_.History()[static_cast<size_t>(selected_history_idx)], cols,
          false);
    }
    selected_history_idx = new_idx;
    if (selected_history_idx >= 0 &&
        selected_history_idx < static_cast<int>(engine_.History().size())) {
      DrawBlockOverlayOnScreen(
          engine_.History()[static_cast<size_t>(selected_history_idx)], cols,
          true);
    }
  };

  redraw_line();

  for (;;) {
    struct pollfd pfd = {};
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    int ready = poll(&pfd, 1, kEditorPollTimeoutMs);
    if (ready <= 0) continue;

    char raw_buf[256];
    ssize_t n = read(STDIN_FILENO, raw_buf, sizeof(raw_buf));
    if (n <= 0) return {LineEditorAction::kExitShell, ""};

    std::string_view raw(raw_buf, static_cast<size_t>(n));
    size_t i = 0;
    while (i < raw.size()) {
      auto ch = static_cast<unsigned char>(raw[i]);

      if (ch == 0x1b) {
        if (i + 1 >= raw.size()) {
          clear_menu_rows();
          menu_items.clear();
          menu_selected = -1;
          set_selected_history(-1);
          redraw_line();
          ++i;
          continue;
        }

        if (raw[i + 1] == 'm' || raw[i + 1] == 'M') {
          clear_menu_rows();
          set_selected_history(-1);
          prefill_buffer_ = buffer;
          term_mode.Restore();
          return {LineEditorAction::kOpenMemoryExplorer, ""};
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
              bool is_release = (final_ch == 'm');
              bool is_motion = ((btn & 32) != 0);
              int hit_idx = -1;
              if (mrow < prompt_row) {
                for (int h = static_cast<int>(engine_.History().size()) - 1;
                     h >= 0; --h) {
                  const auto& blk = engine_.History()[static_cast<size_t>(h)];
                  if (blk.start_screen_row >= 1 &&
                      mrow >= blk.start_screen_row &&
                      mrow <= blk.end_screen_row) {
                    hit_idx = h;
                    break;
                  }
                }
              }
              if (is_motion) {
                if (hit_idx != hovered_history_idx) {
                  if (hovered_history_idx >= 0 &&
                      hovered_history_idx != selected_history_idx &&
                      hovered_history_idx <
                          static_cast<int>(engine_.History().size())) {
                    DrawBlockOverlayOnScreen(
                        engine_.History()[static_cast<size_t>(
                            hovered_history_idx)],
                        cols, false);
                  }
                  hovered_history_idx = hit_idx;
                  if (hovered_history_idx >= 0 &&
                      hovered_history_idx <
                          static_cast<int>(engine_.History().size())) {
                    DrawBlockOverlayOnScreen(
                        engine_.History()[static_cast<size_t>(
                            hovered_history_idx)],
                        cols, true);
                    std::cout << "\x1b]22;pointer\x07" << std::flush;
                  } else {
                    std::cout << "\x1b]22;\x07" << std::flush;
                  }
                }
              } else if (!is_release && (btn & 3) == 0 && hit_idx >= 0) {
                const auto& blk =
                    engine_.History()[static_cast<size_t>(hit_idx)];
                if (blk.kind == HistoryBlockKind::kQuery) {
                  set_selected_history(-1);
                  buffer = blk.text;
                  cursor = buffer.size();
                  redraw_line();
                } else if (blk.has_js_value) {
                  set_selected_history(-1);
                  clear_menu_rows();
                  term_mode.Restore();
                  RunObjectInspector(
                      engine_, blk.js_value,
                      "Response #" + std::to_string(blk.turn_index));
                  prefill_buffer_ = buffer;
                  return ReadCommand(turn_index);
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
              clear_menu_rows();
              menu_items.clear();
              menu_selected = -1;
              set_selected_history(-1);
              redraw_line();
              continue;
            }
            if ((cp == 'm' || cp == 'M') && (mods & 4) != 0) {
              clear_menu_rows();
              set_selected_history(-1);
              prefill_buffer_ = buffer;
              term_mode.Restore();
              return {LineEditorAction::kOpenMemoryExplorer, ""};
            }
            if ((cp == 'c' || cp == 'C') && (mods & 4) != 0) {
              ch = kCtrlC;
            } else if ((cp == 'd' || cp == 'D') && (mods & 4) != 0) {
              ch = kCtrlD;
            } else if ((cp == 'l' || cp == 'L') && (mods & 4) != 0) {
              ch = kCtrlL;
            } else if ((cp == 'a' || cp == 'A') && (mods & 4) != 0) {
              ch = kCtrlA;
            } else if ((cp == 'e' || cp == 'E') && (mods & 4) != 0) {
              ch = kCtrlE;
            } else if ((cp == 'u' || cp == 'U') && (mods & 4) != 0) {
              ch = kCtrlU;
            } else if ((cp == 'k' || cp == 'K') && (mods & 4) != 0) {
              ch = kCtrlK;
            } else if ((cp == 'w' || cp == 'W') && (mods & 4) != 0) {
              ch = kCtrlW;
            } else if (cp == 9 && (mods & 1) != 0) {
              final_ch = 'Z';
            } else if (cp == 13) {
              ch = '\r';
            } else if (cp == 9) {
              ch = '\t';
            } else if (cp == 127) {
              ch = 127;
            } else {
              continue;
            }
          }

          if (final_ch == 'A') {
            clear_menu_rows();
            menu_items.clear();
            menu_selected = -1;
            if (!engine_.History().empty()) {
              if (selected_history_idx < 0) {
                saved_live_buffer = buffer;
                set_selected_history(
                    static_cast<int>(engine_.History().size()) - 1);
              } else if (selected_history_idx > 0) {
                set_selected_history(selected_history_idx - 1);
              }
              redraw_line();
            }
            continue;
          }
          if (final_ch == 'B') {
            clear_menu_rows();
            menu_items.clear();
            menu_selected = -1;
            if (selected_history_idx >= 0) {
              if (selected_history_idx + 1 <
                  static_cast<int>(engine_.History().size())) {
                set_selected_history(selected_history_idx + 1);
              } else {
                set_selected_history(-1);
                buffer = saved_live_buffer;
                cursor = buffer.size();
              }
              redraw_line();
            }
            continue;
          }
          if (final_ch == 'C') {
            set_selected_history(-1);
            std::string ghost = compute_ghost_suffix();
            if (!ghost.empty() && cursor == buffer.size()) {
              buffer += ghost;
              cursor = buffer.size();
            } else if (cursor < buffer.size()) {
              ++cursor;
            }
            clear_menu_rows();
            menu_items.clear();
            menu_selected = -1;
            redraw_line();
            continue;
          }
          if (final_ch == 'D') {
            set_selected_history(-1);
            if (cursor > 0) --cursor;
            clear_menu_rows();
            menu_items.clear();
            menu_selected = -1;
            redraw_line();
            continue;
          }
          if (final_ch == 'H') {
            set_selected_history(-1);
            cursor = 0;
            redraw_line();
            continue;
          }
          if (final_ch == 'F') {
            set_selected_history(-1);
            std::string ghost = compute_ghost_suffix();
            if (!ghost.empty() && cursor == buffer.size()) buffer += ghost;
            cursor = buffer.size();
            redraw_line();
            continue;
          }
          if (final_ch == '~' && body.starts_with("3")) {
            set_selected_history(-1);
            if (cursor < buffer.size()) buffer.erase(cursor, 1);
            clear_menu_rows();
            menu_items.clear();
            menu_selected = -1;
            redraw_line();
            continue;
          }
          if (final_ch == 'Z') {
            set_selected_history(-1);
            if (!menu_items.empty()) {
              menu_selected =
                  (menu_selected + static_cast<int>(menu_items.size()) - 1) %
                  static_cast<int>(menu_items.size());
              const std::string& ins =
                  menu_items[static_cast<size_t>(menu_selected)].insert_text;
              buffer.replace(menu_replace_start,
                             cursor - menu_replace_start, ins);
              cursor = menu_replace_start + ins.size();
              redraw_line();
            }
            continue;
          }
          if (final_ch != 'u') continue;
        } else {
          ++i;
          continue;
        }
      } else {
        ++i;
      }

      if (ch == '\t') {
        set_selected_history(-1);
        if (menu_items.empty()) {
          CompletionContext ctx = AnalyzeCompletionContext(buffer, cursor);
          menu_items = engine_.GetCompletions(buffer, cursor);
          if (!menu_items.empty()) {
            menu_replace_start = ctx.replace_start;
            menu_original_prefix = ctx.prefix;
            menu_selected = 0;
            const std::string& ins = menu_items[0].insert_text;
            buffer.replace(menu_replace_start,
                           cursor - menu_replace_start, ins);
            cursor = menu_replace_start + ins.size();
          }
        } else {
          menu_selected =
              (menu_selected + 1) % static_cast<int>(menu_items.size());
          const std::string& ins =
              menu_items[static_cast<size_t>(menu_selected)].insert_text;
          buffer.replace(menu_replace_start,
                         cursor - menu_replace_start, ins);
          cursor = menu_replace_start + ins.size();
        }
        redraw_line();
        continue;
      }

      if (ch == '\r' || ch == '\n') {
        if (selected_history_idx >= 0 &&
            selected_history_idx < static_cast<int>(engine_.History().size())) {
          const auto& blk =
              engine_.History()[static_cast<size_t>(selected_history_idx)];
          if (blk.kind == HistoryBlockKind::kQuery) {
            buffer = blk.text;
            cursor = buffer.size();
            set_selected_history(-1);
            redraw_line();
            continue;
          }
          if (blk.has_js_value) {
            set_selected_history(-1);
            clear_menu_rows();
            term_mode.Restore();
            RunObjectInspector(engine_, blk.js_value,
                               "Response #" + std::to_string(blk.turn_index));
            prefill_buffer_ = buffer;
            return ReadCommand(turn_index);
          }
        }

        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();

        std::string candidate =
            accumulated.empty() ? buffer : (accumulated + "\n" + buffer);
        if (IsSlashCommandInput(candidate) ||
            IsJavaScriptInputComplete(candidate)) {
          std::cout << "\n" << std::flush;
          int after_row = prompt_row;
          int after_col = 1;
          if (engine_.QueryCursorPosition(after_row, after_col) &&
              prompt_row == rows && after_row == rows) {
            engine_.NotifyTerminalScrolled(1, rows);
          }
          term_mode.Restore();
          return {LineEditorAction::kExecuteInput, candidate};
        }

        accumulated = candidate;
        buffer.clear();
        cursor = 0;
        is_continuation = true;
        prompt_str = FormatPromptString(engine_.Cwd(), true, prompt_cols);
        std::cout << "\n" << std::flush;
        if (prompt_row < rows) {
          ++prompt_row;
        } else {
          engine_.NotifyTerminalScrolled(1, rows);
        }
        redraw_line();
        continue;
      }

      if (ch == kCtrlC) {
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        set_selected_history(-1);
        accumulated.clear();
        buffer.clear();
        cursor = 0;
        is_continuation = false;
        prompt_str = FormatPromptString(engine_.Cwd(), false, prompt_cols);
        std::cout << "^C\n" << std::flush;
        if (prompt_row < rows) {
          ++prompt_row;
        } else {
          engine_.NotifyTerminalScrolled(1, rows);
        }
        redraw_line();
        continue;
      }

      if (ch == kCtrlD) {
        if (buffer.empty() && accumulated.empty()) {
          clear_menu_rows();
          set_selected_history(-1);
          std::cout << "\n" << std::flush;
          term_mode.Restore();
          return {LineEditorAction::kExitShell, ""};
        }
        if (cursor < buffer.size()) buffer.erase(cursor, 1);
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
        continue;
      }

      if (ch == kCtrlL) {
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        set_selected_history(-1);
        engine_.ClearHistoryScreenCoordinates();
        std::cout << "\x1b[2J\x1b[H" << std::flush;
        prompt_row = 1;
        redraw_line();
        continue;
      }

      if (ch == kCtrlA) {
        set_selected_history(-1);
        cursor = 0;
        redraw_line();
        continue;
      }

      if (ch == kCtrlE) {
        set_selected_history(-1);
        cursor = buffer.size();
        redraw_line();
        continue;
      }

      if (ch == kCtrlU) {
        set_selected_history(-1);
        buffer.erase(0, cursor);
        cursor = 0;
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
        continue;
      }

      if (ch == kCtrlK) {
        set_selected_history(-1);
        buffer.erase(cursor);
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
        continue;
      }

      if (ch == kCtrlW) {
        set_selected_history(-1);
        while (cursor > 0 && buffer[cursor - 1] == ' ') {
          buffer.erase(cursor - 1, 1);
          --cursor;
        }
        while (cursor > 0 && buffer[cursor - 1] != ' ') {
          buffer.erase(cursor - 1, 1);
          --cursor;
        }
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
        continue;
      }

      if (ch == 0x7f || ch == '\b') {
        set_selected_history(-1);
        if (cursor > 0) {
          buffer.erase(cursor - 1, 1);
          --cursor;
        }
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
        continue;
      }

      if (ch >= 32 && ch < 127) {
        if (selected_history_idx >= 0) {
          const auto& blk =
              engine_.History()[static_cast<size_t>(selected_history_idx)];
          if (blk.kind == HistoryBlockKind::kQuery) {
            buffer = blk.text;
            cursor = buffer.size();
          }
          set_selected_history(-1);
        }
        buffer.insert(cursor, 1, static_cast<char>(ch));
        ++cursor;
        clear_menu_rows();
        menu_items.clear();
        menu_selected = -1;
        redraw_line();
      }
    }
  }
}
