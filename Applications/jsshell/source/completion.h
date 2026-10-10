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

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// Execution mode selected by command-line arguments.
enum class CliMode {
  kRepl,
  kInlineScript,
  kFileScript,
};

// Parsed command-line invocation parameters for jsshell.
struct ParsedCliArgs {
  CliMode mode = CliMode::kRepl;
  std::string code_or_path;
  std::vector<std::string> script_args;
};

// Parses command-line arguments into REPL, --script inline code, or file script
// execution mode.
ParsedCliArgs ParseCliArguments(int argc, const char* const* argv);

// Classification of the token or expression under the cursor for completion.
enum class CompletionKind {
  kNone,
  kSlashCommand,
  kSlashArgument,
  kIdentifier,
  kProperty,
  kCommandMethod,
  kStringTarget,
  kStringPath,
};

// Extracted completion context at a cursor position in the input buffer.
struct CompletionContext {
  CompletionKind kind = CompletionKind::kNone;
  std::string receiver;
  std::string prefix;
  size_t replace_start = 0;
  bool add_closing_quote = false;
};

// A single completion candidate with optional call signature and documentation.
struct CompletionItem {
  std::string insert_text;
  std::string display_text;
  std::string signature;
  std::string description;
};

// Analyzes the input buffer up to cursor_pos and determines what kind of
// completion should be offered.
CompletionContext AnalyzeCompletionContext(std::string_view line,
                                           size_t cursor_pos);

// Returns true if the JavaScript source string has balanced brackets,
// parentheses, braces, strings, and template literals and does not end with a
// trailing continuation operator.
bool IsJavaScriptInputComplete(std::string_view input);

// Transforms top-level REPL `let`, `const`, and `var` declarations into
// assignments on `globalThis` so variables persist across async evaluations and
// can be redeclared in the REPL.
std::string TransformReplTopLevelDeclarations(std::string_view input);

// Normalizes a relative or absolute filesystem path against cwd, collapsing
// `.` and `..` segments and ensuring a leading `/`.
std::string NormalizeLexicalPath(std::string_view cwd,
                                 std::string_view input_path);

// Matches a text string against a glob pattern supporting `*`, `**`, and `?`.
bool MatchesGlobPattern(std::string_view pattern, std::string_view text);
