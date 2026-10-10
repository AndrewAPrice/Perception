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

#include "completion.h"

#include <algorithm>
#include <array>

#include "slash_commands.h"

namespace {

// Command-line flag for inline JavaScript execution mode.
constexpr std::string_view kInlineScriptFlag = "--script";

// Whitespace characters skipped during lexical analysis.
constexpr std::string_view kWhitespace = " \t\r\n";

// Receiver name used for Command and Pipeline method completion.
constexpr std::string_view kCommandReceiverName = "Command";

// Prefix for filesystem namespace calls taking path string arguments.
constexpr std::string_view kFsDotPrefix = "fs.";

// Call name for pipe.file path completion.
constexpr std::string_view kPipeFileCall = "pipe.file";

// Call name for run target completion.
constexpr std::string_view kRunCall = "run";

// Root directory path string.
constexpr std::string_view kRootPath = "/";

// Current directory path segment.
constexpr std::string_view kCurrentDirSegment = ".";

// Parent directory path segment.
constexpr std::string_view kParentDirSegment = "..";

// Method suffixes on Command/Pipeline that take a file path argument.
constexpr std::array<std::string_view, 3> kPathMethodSuffixes = {
    ".out",
    ".err",
    ".tee",
};

// Functions and methods that return a Command or Pipeline instance.
constexpr std::array<std::string_view, 13> kPipelineProducingCalls = {
    "run",       "pipe",       "pipe.from", "pipe.file", "pipe.seq",
    "pipe.merge", ".pipe",     ".out",      ".err",      ".errToOut",
    ".nullOut",  ".nullErr",   ".tee",
};

// Multi-character operators that indicate an incomplete trailing expression.
constexpr std::array<std::string_view, 15> kTrailingMultiCharOps = {
    "===", "!==", "==", "!=", "<=", ">=", "&&", "||",
    "??",  "=>",  "+=", "-=", "*=", "/=", "**",
};

// Single-character operators that indicate an incomplete trailing expression.
constexpr std::string_view kTrailingSingleCharOps = "+-*/%=<>|&^!~.,?:";

// Declaration keywords rewritten at REPL top level.
constexpr std::array<std::string_view, 3> kDeclarationKeywords = {
    "let",
    "const",
    "var",
};

bool IsAsciiSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool IsIdentStart(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
         c == '$';
}

bool IsIdentChar(char c) {
  return IsIdentStart(c) || (c >= '0' && c <= '9');
}

bool IsAllDigits(std::string_view text) {
  if (text.empty()) return false;
  for (char c : text) {
    if (c < '0' || c > '9') return false;
  }
  return true;
}

std::string_view TrimWhitespace(std::string_view text) {
  size_t first = text.find_first_not_of(kWhitespace);
  if (first == std::string_view::npos) return {};
  size_t last = text.find_last_not_of(kWhitespace);
  return text.substr(first, last - first + 1);
}

enum class LexState {
  kNormal,
  kSingleQuote,
  kDoubleQuote,
  kTemplate,
  kLineComment,
  kBlockComment,
};

// Finds the matching opening delimiter for a closing ')', ']', or '}' at
// `close_pos`. Returns std::string_view::npos if unbalanced.
size_t FindMatchingOpenBracket(std::string_view text, size_t close_pos) {
  if (close_pos >= text.size()) return std::string_view::npos;
  std::vector<size_t> open_stack;
  LexState state = LexState::kNormal;
  for (size_t i = 0; i <= close_pos; ++i) {
    char c = text[i];
    switch (state) {
      case LexState::kNormal:
        if (c == '\'') {
          state = LexState::kSingleQuote;
        } else if (c == '"') {
          state = LexState::kDoubleQuote;
        } else if (c == '`') {
          state = LexState::kTemplate;
        } else if (c == '/' && i + 1 <= close_pos && text[i + 1] == '/') {
          state = LexState::kLineComment;
          ++i;
        } else if (c == '/' && i + 1 <= close_pos && text[i + 1] == '*') {
          state = LexState::kBlockComment;
          ++i;
        } else if (c == '(' || c == '[' || c == '{') {
          open_stack.push_back(i);
        } else if (c == ')' || c == ']' || c == '}') {
          if (open_stack.empty()) return std::string_view::npos;
          size_t open_idx = open_stack.back();
          open_stack.pop_back();
          if (i == close_pos) return open_idx;
        }
        break;
      case LexState::kSingleQuote:
        if (c == '\\' && i + 1 <= close_pos)
          ++i;
        else if (c == '\'')
          state = LexState::kNormal;
        break;
      case LexState::kDoubleQuote:
        if (c == '\\' && i + 1 <= close_pos)
          ++i;
        else if (c == '"')
          state = LexState::kNormal;
        break;
      case LexState::kTemplate:
        if (c == '\\' && i + 1 <= close_pos)
          ++i;
        else if (c == '`')
          state = LexState::kNormal;
        break;
      case LexState::kLineComment:
        if (c == '\n') state = LexState::kNormal;
        break;
      case LexState::kBlockComment:
        if (c == '*' && i + 1 <= close_pos && text[i + 1] == '/') {
          state = LexState::kNormal;
          ++i;
        }
        break;
    }
  }
  return std::string_view::npos;
}

std::string_view ExtractCalleeBeforeParen(std::string_view text,
                                          size_t open_paren_pos) {
  size_t end = open_paren_pos;
  while (end > 0 && IsAsciiSpace(text[end - 1])) --end;
  size_t start = end;
  while (start > 0 && (IsIdentChar(text[start - 1]) || text[start - 1] == '.'))
    --start;
  return text.substr(start, end - start);
}

bool IsPipelineCallReceiver(std::string_view text_before_dot) {
  std::string_view trimmed = TrimWhitespace(text_before_dot);
  if (trimmed.empty() || trimmed.back() != ')') return false;
  size_t open_paren = FindMatchingOpenBracket(trimmed, trimmed.size() - 1);
  if (open_paren == std::string_view::npos) return false;
  std::string_view callee = ExtractCalleeBeforeParen(trimmed, open_paren);
  if (callee.empty()) return false;
  for (std::string_view candidate : kPipelineProducingCalls) {
    if (callee == candidate) return true;
    if (candidate.front() == '.' && callee.size() >= candidate.size() &&
        callee.substr(callee.size() - candidate.size()) == candidate)
      return true;
  }
  return false;
}

bool IsPathStringCallee(std::string_view callee) {
  if (callee == kPipeFileCall) return true;
  if (callee.substr(0, kFsDotPrefix.size()) == kFsDotPrefix) return true;
  for (std::string_view suffix : kPathMethodSuffixes) {
    if (callee == suffix) return true;
    if (callee.size() >= suffix.size() &&
        callee.substr(callee.size() - suffix.size()) == suffix)
      return true;
  }
  return false;
}

CompletionContext AnalyzeSlashCompletion(std::string_view line,
                                         std::string_view head,
                                         size_t slash_pos) {
  size_t cmd_end = slash_pos;
  while (cmd_end < head.size() && !IsAsciiSpace(head[cmd_end])) ++cmd_end;

  if (cmd_end == head.size())
    return CompletionContext{CompletionKind::kSlashCommand, "",
                             std::string(head.substr(slash_pos)), slash_pos,
                             false};

  std::string command(head.substr(slash_pos, cmd_end - slash_pos));
  size_t arg_start = head.size();
  bool in_single_quote = false;
  bool in_double_quote = false;
  char active_quote = '\0';

  for (size_t i = cmd_end; i < head.size(); ++i) {
    char c = head[i];
    if (in_single_quote) {
      if (c == '\'') {
        in_single_quote = false;
        active_quote = '\0';
      }
      continue;
    }
    if (in_double_quote) {
      if (c == '\\' && i + 1 < head.size()) {
        ++i;
      } else if (c == '"') {
        in_double_quote = false;
        active_quote = '\0';
      }
      continue;
    }
    if (IsAsciiSpace(c)) {
      arg_start = i + 1;
    } else if (c == '\'') {
      in_single_quote = true;
      active_quote = '\'';
      arg_start = i + 1;
    } else if (c == '"') {
      in_double_quote = true;
      active_quote = '"';
      arg_start = i + 1;
    }
  }

  bool add_closing_quote = false;
  if (active_quote != '\0')
    add_closing_quote =
        (head.size() >= line.size() || line[head.size()] != active_quote);

  return CompletionContext{
      CompletionKind::kSlashArgument,
      command,
      std::string(head.substr(arg_start)),
      arg_start,
      add_closing_quote,
  };
}

bool EndsWithContinuationOperator(std::string_view stripped) {
  std::string_view trimmed = TrimWhitespace(stripped);
  if (trimmed.empty()) return false;

  if (trimmed.size() >= 2) {
    std::string_view last_two = trimmed.substr(trimmed.size() - 2);
    if (last_two == "++" || last_two == "--") return false;
  }

  for (std::string_view op : kTrailingMultiCharOps) {
    if (trimmed.size() >= op.size() &&
        trimmed.substr(trimmed.size() - op.size()) == op)
      return true;
  }

  char last = trimmed.back();
  return kTrailingSingleCharOps.find(last) != std::string_view::npos;
}

bool MatchGlobHere(std::string_view pattern, size_t pi, std::string_view text,
                   size_t ti) {
  while (pi < pattern.size()) {
    if (pattern[pi] == '*') {
      bool is_double_star =
          (pi + 1 < pattern.size() && pattern[pi + 1] == '*');
      while (pi + 1 < pattern.size() && pattern[pi + 1] == '*') ++pi;

      if (is_double_star) {
        if (pi + 1 < pattern.size() && pattern[pi + 1] == '/') {
          if (MatchGlobHere(pattern, pi + 2, text, ti)) return true;
          if (ti == 0 && !text.empty() && text[0] == '/' &&
              MatchGlobHere(pattern, pi + 2, text, 1))
            return true;
        }
        for (size_t next_ti = ti; next_ti <= text.size(); ++next_ti) {
          if (MatchGlobHere(pattern, pi + 1, text, next_ti)) return true;
        }
        return false;
      }

      for (size_t next_ti = ti; next_ti <= text.size(); ++next_ti) {
        if (MatchGlobHere(pattern, pi + 1, text, next_ti)) return true;
        if (next_ti < text.size() && text[next_ti] == '/') break;
      }
      return false;
    }

    if (ti >= text.size()) return false;

    if (pattern[pi] == '?') {
      if (text[ti] == '/') return false;
      ++pi;
      ++ti;
      continue;
    }

    if (pattern[pi] != text[ti]) return false;
    ++pi;
    ++ti;
  }

  return ti == text.size();
}

bool MatchDeclarationKeyword(std::string_view input, size_t pos,
                             std::string_view& keyword_out,
                             size_t& after_keyword_out) {
  if (pos > 0 && (IsIdentChar(input[pos - 1]) || input[pos - 1] == '.'))
    return false;
  for (std::string_view kw : kDeclarationKeywords) {
    if (pos + kw.size() < input.size() &&
        input.substr(pos, kw.size()) == kw &&
        IsAsciiSpace(input[pos + kw.size()])) {
      keyword_out = kw;
      after_keyword_out = pos + kw.size();
      return true;
    }
  }
  return false;
}

bool NextLineContinuesExpression(std::string_view rest_after_newline) {
  size_t next_non_space = rest_after_newline.find_first_not_of(" \t\r");
  if (next_non_space == std::string_view::npos) return false;
  char c = rest_after_newline[next_non_space];
  if (c == '.' || c == '(' || c == '[' || c == '?' || c == ':') return true;
  if (rest_after_newline.substr(next_non_space, 2) == "&&" ||
      rest_after_newline.substr(next_non_space, 2) == "||" ||
      rest_after_newline.substr(next_non_space, 2) == "??" ||
      rest_after_newline.substr(next_non_space, 2) == "=>")
    return true;
  return false;
}

}  // namespace

ParsedCliArgs ParseCliArguments(int argc, const char* const* argv) {
  ParsedCliArgs parsed;
  if (argc <= 1 || argv == nullptr || argv[1] == nullptr) {
    parsed.mode = CliMode::kRepl;
    return parsed;
  }

  std::string_view first_arg(argv[1]);
  if (first_arg == kInlineScriptFlag) {
    parsed.mode = CliMode::kInlineScript;
    for (int i = 2; i < argc; ++i) {
      if (argv[i] == nullptr) continue;
      if (!parsed.code_or_path.empty()) parsed.code_or_path.push_back(' ');
      parsed.code_or_path.append(argv[i]);
    }
    return parsed;
  }

  parsed.mode = CliMode::kFileScript;
  parsed.code_or_path = std::string(first_arg);
  for (int i = 2; i < argc; ++i) {
    if (argv[i] != nullptr) parsed.script_args.emplace_back(argv[i]);
  }
  return parsed;
}

CompletionContext AnalyzeCompletionContext(std::string_view line,
                                           size_t cursor_pos) {
  cursor_pos = std::min(cursor_pos, line.size());
  std::string_view head = line.substr(0, cursor_pos);

  if (IsSlashCommandInput(line)) {
    size_t slash_pos = head.find_first_not_of(kWhitespace);
    if (slash_pos != std::string_view::npos && head[slash_pos] == '/')
      return AnalyzeSlashCompletion(line, head, slash_pos);
  }

  LexState state = LexState::kNormal;
  size_t quote_start = std::string_view::npos;
  char quote_char = '\0';
  std::vector<bool> template_brace_stack;

  for (size_t i = 0; i < head.size(); ++i) {
    char c = head[i];
    switch (state) {
      case LexState::kNormal:
        if (c == '\'') {
          state = LexState::kSingleQuote;
          quote_start = i;
          quote_char = '\'';
        } else if (c == '"') {
          state = LexState::kDoubleQuote;
          quote_start = i;
          quote_char = '"';
        } else if (c == '`') {
          state = LexState::kTemplate;
        } else if (c == '/' && i + 1 < head.size() && head[i + 1] == '/') {
          state = LexState::kLineComment;
          ++i;
        } else if (c == '/' && i + 1 < head.size() && head[i + 1] == '*') {
          state = LexState::kBlockComment;
          ++i;
        } else if (c == '{' && !template_brace_stack.empty()) {
          template_brace_stack.push_back(false);
        } else if (c == '}' && !template_brace_stack.empty()) {
          bool returns_to_template = template_brace_stack.back();
          template_brace_stack.pop_back();
          if (returns_to_template) state = LexState::kTemplate;
        }
        break;
      case LexState::kSingleQuote:
        if (c == '\\' && i + 1 < head.size()) {
          ++i;
        } else if (c == '\'') {
          state = LexState::kNormal;
          quote_start = std::string_view::npos;
          quote_char = '\0';
        }
        break;
      case LexState::kDoubleQuote:
        if (c == '\\' && i + 1 < head.size()) {
          ++i;
        } else if (c == '"') {
          state = LexState::kNormal;
          quote_start = std::string_view::npos;
          quote_char = '\0';
        }
        break;
      case LexState::kTemplate:
        if (c == '\\' && i + 1 < head.size()) {
          ++i;
        } else if (c == '$' && i + 1 < head.size() && head[i + 1] == '{') {
          template_brace_stack.push_back(true);
          state = LexState::kNormal;
          ++i;
        } else if (c == '`') {
          state = LexState::kNormal;
        }
        break;
      case LexState::kLineComment:
        if (c == '\n') state = LexState::kNormal;
        break;
      case LexState::kBlockComment:
        if (c == '*' && i + 1 < head.size() && head[i + 1] == '/') {
          state = LexState::kNormal;
          ++i;
        }
        break;
    }
  }

  if (state == LexState::kLineComment || state == LexState::kBlockComment ||
      state == LexState::kTemplate)
    return CompletionContext{CompletionKind::kNone, "", "", cursor_pos, false};

  if (state == LexState::kSingleQuote || state == LexState::kDoubleQuote) {
    size_t before_quote = quote_start;
    while (before_quote > 0 && IsAsciiSpace(head[before_quote - 1]))
      --before_quote;
    if (before_quote > 0 && head[before_quote - 1] == '(') {
      std::string_view callee =
          ExtractCalleeBeforeParen(head, before_quote - 1);
      bool add_closing_quote =
          (cursor_pos >= line.size() || line[cursor_pos] != quote_char);
      std::string prefix(head.substr(quote_start + 1));
      if (callee == kRunCall)
        return CompletionContext{CompletionKind::kStringTarget,
                                 std::string(callee), prefix, quote_start + 1,
                                 add_closing_quote};
      if (IsPathStringCallee(callee))
        return CompletionContext{CompletionKind::kStringPath,
                                 std::string(callee), prefix, quote_start + 1,
                                 add_closing_quote};
    }
    return CompletionContext{CompletionKind::kNone, "", "", cursor_pos, false};
  }

  size_t ident_start = head.size();
  while (ident_start > 0 && IsIdentChar(head[ident_start - 1])) --ident_start;
  std::string prefix(head.substr(ident_start));

  size_t before_ident = ident_start;
  while (before_ident > 0 && IsAsciiSpace(head[before_ident - 1]))
    --before_ident;

  if (before_ident > 0 && head[before_ident - 1] == '.') {
    size_t dot_pos = before_ident - 1;
    if (dot_pos > 0 && head[dot_pos - 1] == '.')
      return CompletionContext{CompletionKind::kNone, "", "", cursor_pos, false};

    std::string_view before_dot = head.substr(0, dot_pos);
    if (IsPipelineCallReceiver(before_dot))
      return CompletionContext{CompletionKind::kCommandMethod,
                               std::string(kCommandReceiverName), prefix,
                               ident_start, false};

    size_t recv_end = dot_pos;
    while (recv_end > 0 && IsAsciiSpace(head[recv_end - 1])) --recv_end;
    size_t recv_start = recv_end;
    while (recv_start > 0) {
      char c = head[recv_start - 1];
      if (IsIdentChar(c) || c == '.') {
        --recv_start;
      } else if (c == ']' || c == ')') {
        size_t open_pos = FindMatchingOpenBracket(head, recv_start - 1);
        if (open_pos == std::string_view::npos) break;
        recv_start = open_pos;
      } else {
        break;
      }
    }

    std::string_view receiver =
        TrimWhitespace(head.substr(recv_start, recv_end - recv_start));
    if (receiver.empty() || IsAllDigits(receiver))
      return CompletionContext{CompletionKind::kNone, "", "", cursor_pos, false};

    return CompletionContext{
        CompletionKind::kProperty,
        std::string(receiver),
        prefix,
        ident_start,
        false,
    };
  }

  return CompletionContext{
      CompletionKind::kIdentifier,
      "",
      prefix,
      ident_start,
      false,
  };
}

bool IsJavaScriptInputComplete(std::string_view input) {
  if (TrimWhitespace(input).empty()) return true;

  LexState state = LexState::kNormal;
  int paren_depth = 0;
  int bracket_depth = 0;
  int brace_depth = 0;
  std::vector<bool> template_brace_stack;
  std::string stripped_normal;
  stripped_normal.reserve(input.size());

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    switch (state) {
      case LexState::kNormal:
        if (c == '\'') {
          state = LexState::kSingleQuote;
          stripped_normal.append("\"s\"");
        } else if (c == '"') {
          state = LexState::kDoubleQuote;
          stripped_normal.append("\"s\"");
        } else if (c == '`') {
          state = LexState::kTemplate;
          stripped_normal.append("\"s\"");
        } else if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
          state = LexState::kLineComment;
          stripped_normal.push_back(' ');
          ++i;
        } else if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
          state = LexState::kBlockComment;
          stripped_normal.push_back(' ');
          ++i;
        } else {
          stripped_normal.push_back(c);
          if (c == '(') {
            ++paren_depth;
          } else if (c == ')') {
            if (paren_depth > 0) --paren_depth;
          } else if (c == '[') {
            ++bracket_depth;
          } else if (c == ']') {
            if (bracket_depth > 0) --bracket_depth;
          } else if (c == '{') {
            ++brace_depth;
            if (!template_brace_stack.empty())
              template_brace_stack.push_back(false);
          } else if (c == '}') {
            if (brace_depth > 0) --brace_depth;
            if (!template_brace_stack.empty()) {
              bool back_to_template = template_brace_stack.back();
              template_brace_stack.pop_back();
              if (back_to_template) state = LexState::kTemplate;
            }
          }
        }
        break;
      case LexState::kSingleQuote:
        if (c == '\\' && i + 1 < input.size())
          ++i;
        else if (c == '\'')
          state = LexState::kNormal;
        break;
      case LexState::kDoubleQuote:
        if (c == '\\' && i + 1 < input.size())
          ++i;
        else if (c == '"')
          state = LexState::kNormal;
        break;
      case LexState::kTemplate:
        if (c == '\\' && i + 1 < input.size()) {
          ++i;
        } else if (c == '$' && i + 1 < input.size() && input[i + 1] == '{') {
          ++brace_depth;
          template_brace_stack.push_back(true);
          state = LexState::kNormal;
          ++i;
        } else if (c == '`') {
          state = LexState::kNormal;
        }
        break;
      case LexState::kLineComment:
        if (c == '\n') {
          state = LexState::kNormal;
          stripped_normal.push_back('\n');
        }
        break;
      case LexState::kBlockComment:
        if (c == '*' && i + 1 < input.size() && input[i + 1] == '/') {
          state = LexState::kNormal;
          ++i;
        }
        break;
    }
  }

  if (state == LexState::kSingleQuote || state == LexState::kDoubleQuote ||
      state == LexState::kTemplate || state == LexState::kBlockComment)
    return false;
  if (paren_depth > 0 || bracket_depth > 0 || brace_depth > 0) return false;
  if (EndsWithContinuationOperator(stripped_normal)) return false;
  return true;
}

std::string TransformReplTopLevelDeclarations(std::string_view input) {
  std::string out;
  out.reserve(input.size() + 64);

  LexState state = LexState::kNormal;
  int depth = 0;
  bool at_statement_start = true;
  std::vector<bool> template_brace_stack;

  size_t i = 0;
  while (i < input.size()) {
    char c = input[i];

    if (state == LexState::kNormal && depth == 0 && at_statement_start) {
      if (IsAsciiSpace(c)) {
        out.push_back(c);
        ++i;
        continue;
      }

      std::string_view kw;
      size_t after_kw = 0;
      if (MatchDeclarationKeyword(input, i, kw, after_kw)) {
        size_t scan = after_kw;
        while (scan < input.size() && (input[scan] == ' ' || input[scan] == '\t'))
          ++scan;

        if (scan < input.size() && IsIdentStart(input[scan])) {
          out.append("var ");
          i = scan;
          bool first_decl = true;

          while (i < input.size()) {
            while (i < input.size() && IsAsciiSpace(input[i])) ++i;
            if (i >= input.size() || !IsIdentStart(input[i])) break;

            size_t id_start = i;
            while (i < input.size() && IsIdentChar(input[i])) ++i;
            std::string_view ident = input.substr(id_start, i - id_start);

            size_t ws_after_id = i;
            while (ws_after_id < input.size() &&
                   (input[ws_after_id] == ' ' || input[ws_after_id] == '\t')) {
              ++ws_after_id;
            }

            if (!first_decl) out.append(", ");
            first_decl = false;

            if (ws_after_id < input.size() && input[ws_after_id] == '=' &&
                (ws_after_id + 1 >= input.size() ||
                 input[ws_after_id + 1] != '=')) {
              i = ws_after_id + 1;
              while (i < input.size() && (input[i] == ' ' || input[i] == '\t'))
                ++i;

              size_t expr_start = i;
              int expr_depth = 0;
              LexState expr_state = LexState::kNormal;
              std::vector<bool> expr_tpl_stack;

              while (i < input.size()) {
                char ec = input[i];
                if (expr_state == LexState::kNormal) {
                  if (expr_depth == 0) {
                    if (ec == ',' || ec == ';') break;
                    if (ec == '\n') {
                      std::string_view expr_so_far =
                          input.substr(expr_start, i - expr_start);
                      if (!TrimWhitespace(expr_so_far).empty() &&
                          IsJavaScriptInputComplete(expr_so_far) &&
                          !NextLineContinuesExpression(input.substr(i + 1)))
                        break;
                    }
                  }
                  if (ec == '\'') {
                    expr_state = LexState::kSingleQuote;
                  } else if (ec == '"') {
                    expr_state = LexState::kDoubleQuote;
                  } else if (ec == '`') {
                    expr_state = LexState::kTemplate;
                  } else if (ec == '/' && i + 1 < input.size() &&
                             input[i + 1] == '/') {
                    expr_state = LexState::kLineComment;
                    ++i;
                  } else if (ec == '/' && i + 1 < input.size() &&
                             input[i + 1] == '*') {
                    expr_state = LexState::kBlockComment;
                    ++i;
                  } else if (ec == '(' || ec == '[' || ec == '{') {
                    ++expr_depth;
                    if (ec == '{' && !expr_tpl_stack.empty())
                      expr_tpl_stack.push_back(false);
                  } else if (ec == ')' || ec == ']' || ec == '}') {
                    if (expr_depth > 0) --expr_depth;
                    if (ec == '}' && !expr_tpl_stack.empty()) {
                      bool to_tpl = expr_tpl_stack.back();
                      expr_tpl_stack.pop_back();
                      if (to_tpl) expr_state = LexState::kTemplate;
                    }
                  }
                } else if (expr_state == LexState::kSingleQuote) {
                  if (ec == '\\' && i + 1 < input.size())
                    ++i;
                  else if (ec == '\'')
                    expr_state = LexState::kNormal;
                } else if (expr_state == LexState::kDoubleQuote) {
                  if (ec == '\\' && i + 1 < input.size())
                    ++i;
                  else if (ec == '"')
                    expr_state = LexState::kNormal;
                } else if (expr_state == LexState::kTemplate) {
                  if (ec == '\\' && i + 1 < input.size()) {
                    ++i;
                  } else if (ec == '$' && i + 1 < input.size() &&
                             input[i + 1] == '{') {
                    ++expr_depth;
                    expr_tpl_stack.push_back(true);
                    expr_state = LexState::kNormal;
                    ++i;
                  } else if (ec == '`') {
                    expr_state = LexState::kNormal;
                  }
                } else if (expr_state == LexState::kLineComment) {
                  if (ec == '\n') {
                    expr_state = LexState::kNormal;
                    if (expr_depth == 0) {
                      std::string_view expr_so_far =
                          input.substr(expr_start, i - expr_start);
                      if (!TrimWhitespace(expr_so_far).empty() &&
                          IsJavaScriptInputComplete(expr_so_far) &&
                          !NextLineContinuesExpression(input.substr(i + 1)))
                        break;
                    }
                  }
                } else if (expr_state == LexState::kBlockComment) {
                  if (ec == '*' && i + 1 < input.size() &&
                      input[i + 1] == '/') {
                    expr_state = LexState::kNormal;
                    ++i;
                  }
                }
                ++i;
              }

              std::string_view expr =
                  TrimWhitespace(input.substr(expr_start, i - expr_start));
              out.append(ident);
              out.append(" = (globalThis.");
              out.append(ident);
              out.append(" = ");
              out.append(expr);
              out.push_back(')');
            } else {
              i = ws_after_id;
              out.append(ident);
              out.append(" = (globalThis.");
              out.append(ident);
              out.append(" = undefined)");
            }

            if (i < input.size() && input[i] == ',') {
              ++i;
              continue;
            }
            break;
          }

          if (i < input.size() && (input[i] == ';' || input[i] == '\n')) {
            out.push_back(input[i]);
            ++i;
            at_statement_start = true;
          } else {
            at_statement_start = false;
          }
          continue;
        }
      }
    }

    switch (state) {
      case LexState::kNormal:
        if (c == '\'') {
          state = LexState::kSingleQuote;
          at_statement_start = false;
        } else if (c == '"') {
          state = LexState::kDoubleQuote;
          at_statement_start = false;
        } else if (c == '`') {
          state = LexState::kTemplate;
          at_statement_start = false;
        } else if (c == '/' && i + 1 < input.size() && input[i + 1] == '/') {
          state = LexState::kLineComment;
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '/' && i + 1 < input.size() && input[i + 1] == '*') {
          state = LexState::kBlockComment;
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '(' || c == '[' || c == '{') {
          ++depth;
          if (c == '{' && !template_brace_stack.empty())
            template_brace_stack.push_back(false);
          at_statement_start = false;
        } else if (c == ')' || c == ']' || c == '}') {
          if (depth > 0) --depth;
          if (c == '}' && !template_brace_stack.empty()) {
            bool to_tpl = template_brace_stack.back();
            template_brace_stack.pop_back();
            if (to_tpl) state = LexState::kTemplate;
          }
          at_statement_start = (depth == 0 && c == '}');
        } else if (depth == 0 && (c == ';' || c == '\n')) {
          at_statement_start = true;
        } else if (!IsAsciiSpace(c)) {
          at_statement_start = false;
        }
        break;
      case LexState::kSingleQuote:
        if (c == '\\' && i + 1 < input.size()) {
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '\'') {
          state = LexState::kNormal;
        }
        break;
      case LexState::kDoubleQuote:
        if (c == '\\' && i + 1 < input.size()) {
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '"') {
          state = LexState::kNormal;
        }
        break;
      case LexState::kTemplate:
        if (c == '\\' && i + 1 < input.size()) {
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '$' && i + 1 < input.size() && input[i + 1] == '{') {
          ++depth;
          template_brace_stack.push_back(true);
          state = LexState::kNormal;
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        } else if (c == '`') {
          state = LexState::kNormal;
        }
        break;
      case LexState::kLineComment:
        if (c == '\n') {
          state = LexState::kNormal;
          if (depth == 0) at_statement_start = true;
        }
        break;
      case LexState::kBlockComment:
        if (c == '*' && i + 1 < input.size() && input[i + 1] == '/') {
          state = LexState::kNormal;
          out.push_back(c);
          out.push_back(input[++i]);
          ++i;
          continue;
        }
        break;
    }

    out.push_back(c);
    ++i;
  }

  return out;
}

std::string NormalizeLexicalPath(std::string_view cwd,
                                 std::string_view input_path) {
  std::vector<std::string_view> segments;
  auto push_segments = [&segments](std::string_view path_str) {
    size_t pos = 0;
    while (pos < path_str.size()) {
      size_t next = path_str.find('/', pos);
      std::string_view part = (next == std::string_view::npos)
                                  ? path_str.substr(pos)
                                  : path_str.substr(pos, next - pos);
      if (!part.empty() && part != kCurrentDirSegment) {
        if (part == kParentDirSegment) {
          if (!segments.empty()) segments.pop_back();
        } else {
          segments.push_back(part);
        }
      }
      if (next == std::string_view::npos) break;
      pos = next + 1;
    }
  };

  if (input_path.empty()) {
    push_segments(cwd);
  } else if (input_path.front() == '/') {
    push_segments(input_path);
  } else {
    push_segments(cwd);
    push_segments(input_path);
  }

  if (segments.empty()) return std::string(kRootPath);

  std::string result;
  for (std::string_view seg : segments) {
    result.push_back('/');
    result.append(seg);
  }
  return result;
}

bool MatchesGlobPattern(std::string_view pattern, std::string_view text) {
  return MatchGlobHere(pattern, 0, text, 0);
}
