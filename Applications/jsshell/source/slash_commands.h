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

#pragma once

#include <string>
#include <string_view>
#include <vector>

// Parsed representation of a REPL slash command line.
struct ParsedSlashCommand {
  bool is_slash_command = false;
  std::string command;
  std::vector<std::string> args;
  std::string raw_args;
};

// Returns true if the input line starts with a REPL slash command (a leading
// `/` that is not a `//` or `/*` comment).
bool IsSlashCommandInput(std::string_view input);

// Splits a shell-style argument string supporting single quotes, double quotes,
// backslash escapes, and mid-token quoted substrings (e.g. `--abc="A F C"`).
std::vector<std::string> SplitShellArguments(std::string_view args_str);

// Parses a line starting with `/` into its command name (including leading `/`)
// and argument list.
ParsedSlashCommand ParseSlashCommand(std::string_view line);
