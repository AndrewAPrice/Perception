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

#include "slash_commands.h"

namespace {

// Characters treated as whitespace when tokenizing slash commands.
constexpr std::string_view kWhitespaceChars = " \t\r\n";

// Prefix character for REPL slash commands.
constexpr char kSlashPrefix = '/';

// Second character of a single-line JavaScript comment.
constexpr char kLineCommentChar = '/';

// Second character of a block JavaScript comment.
constexpr char kBlockCommentChar = '*';

bool IsWhitespace(char c) {
  return kWhitespaceChars.find(c) != std::string_view::npos;
}

}  // namespace

bool IsSlashCommandInput(std::string_view input) {
  size_t pos = input.find_first_not_of(kWhitespaceChars);
  if (pos == std::string_view::npos) return false;
  if (input[pos] != kSlashPrefix) return false;
  if (pos + 1 < input.size() &&
      (input[pos + 1] == kLineCommentChar ||
       input[pos + 1] == kBlockCommentChar))
    return false;
  return true;
}

std::vector<std::string> SplitShellArguments(std::string_view args_str) {
  std::vector<std::string> args;
  std::string current;
  bool in_token = false;
  bool in_single_quote = false;
  bool in_double_quote = false;

  for (size_t i = 0; i < args_str.size(); ++i) {
    char c = args_str[i];
    if (in_single_quote) {
      if (c == '\'')
        in_single_quote = false;
      else
        current.push_back(c);
      continue;
    }

    if (in_double_quote) {
      if (c == '"') {
        in_double_quote = false;
      } else if (c == '\\' && i + 1 < args_str.size()) {
        current.push_back(args_str[++i]);
      } else {
        current.push_back(c);
      }
      continue;
    }

    if (IsWhitespace(c)) {
      if (in_token) {
        args.push_back( std::move(current));
        current.clear();
        in_token = false;
      }
      continue;
    }

    in_token = true;
    if (c == '\'') {
      in_single_quote = true;
    } else if (c == '"') {
      in_double_quote = true;
    } else if (c == '\\' && i + 1 < args_str.size()) {
      current.push_back(args_str[++i]);
    } else {
      current.push_back(c);
    }
  }

  if (in_token) args.push_back(std::move(current));
  return args;
}

ParsedSlashCommand ParseSlashCommand(std::string_view line) {
  ParsedSlashCommand parsed;
  if (!IsSlashCommandInput(line)) return parsed;

  size_t start = line.find_first_not_of(kWhitespaceChars);
  size_t cmd_end = start;
  while (cmd_end < line.size() && !IsWhitespace(line[cmd_end])) ++cmd_end;

  parsed.is_slash_command = true;
  parsed.command = std::string(line.substr(start, cmd_end - start));

  size_t args_start = line.find_first_not_of(kWhitespaceChars, cmd_end);
  if (args_start != std::string_view::npos) {
    size_t args_end = line.find_last_not_of("\r\n");
    if (args_end != std::string_view::npos && args_end >= args_start)
      parsed.raw_args =
          std::string(line.substr(args_start, args_end - args_start + 1));
    parsed.args = SplitShellArguments(parsed.raw_args);
  }

  return parsed;
}
