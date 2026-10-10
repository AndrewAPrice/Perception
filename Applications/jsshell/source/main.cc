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

#include <cctype>
#include <iostream>
#include <string>
#include <string_view>

#include "completion.h"
#include "js_engine.h"
#include "line_editor.h"
#include "object_inspector.h"
#include "slash_commands.h"

namespace {

// OSC 0 sequence setting the default jsshell window title.
constexpr std::string_view kDefaultWindowTitle =
    "\x1b]0;jsshell — Perception JavaScript Shell\x07";

// Interactive REPL startup banner.
constexpr std::string_view kWelcomeBanner =
    "\x1b[1;38;2;137;180;250mjsshell\x1b[0;38;2;166;173;200m — Perception "
    "JavaScript Shell \x1b[38;2;108;112;134m|\x1b[38;2;166;173;200m Type "
    "\x1b[1;38;2;166;227;161m/help\x1b[0;38;2;166;173;200m for API & Cookbook, "
    "\x1b[1;38;2;249;226;175mCtrl+M\x1b[0;38;2;166;173;200m for Memory "
    "Explorer\x1b[0m\n";

// Virtual filename used for interactive REPL evaluations.
constexpr std::string_view kReplFilename = "<repl>";

bool HasNonWhitespace(std::string_view text) {
  for (char ch : text) {
    if (!std::isspace(static_cast<unsigned char>(ch))) return true;
  }
  return false;
}

}  // namespace

int main(int argc, char* argv[]) {
  ParsedCliArgs parsed = ParseCliArguments(argc, argv);
  JsEngine engine;

  if (parsed.mode == CliMode::kInlineScript) {
    bool ok = engine.RunInlineScript(parsed.code_or_path);
    engine.RestoreTerminalState();
    return ok ? 0 : 1;
  }

  if (parsed.mode == CliMode::kFileScript) {
    bool ok = engine.RunScriptFile(parsed.code_or_path, parsed.script_args);
    engine.RestoreTerminalState();
    return ok ? 0 : 1;
  }

  std::cout << kDefaultWindowTitle << kWelcomeBanner << std::flush;

  LineEditor editor(engine);
  size_t turn_index = 1;

  while (true) {
    LineEditorResult res = editor.ReadCommand(turn_index);
    if (res.action == LineEditorAction::kExitShell) break;

    if (res.action == LineEditorAction::kOpenMemoryExplorer) {
      RunMemoryExplorer(engine);
      continue;
    }

    if (!HasNonWhitespace(res.input)) continue;

    if (IsSlashCommandInput(res.input)) {
      engine.RecordQueryBlock(res.input, turn_index);
      bool open_mem = false;
      if (!engine.ExecuteSlashCommand(res.input, turn_index, open_mem)) break;
      if (open_mem) RunMemoryExplorer(engine);
      ++turn_index;
      continue;
    }

    engine.RecordQueryBlock(res.input, turn_index);
    bool had_exception = false;
    JSValue val =
        engine.EvaluateAsync(res.input, kReplFilename, true, had_exception);
    if (!had_exception) engine.HandleReplReturnValue(val, turn_index);
    JS_FreeValue(engine.Context(), val);
    engine.RestoreTerminalState();
    ++turn_index;
  }

  engine.RestoreTerminalState();
  return 0;
}
