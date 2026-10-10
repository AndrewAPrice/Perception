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

#include "js_engine.h"

// Outcome of reading a single interactive command or action in the REPL editor.
enum class LineEditorAction {
  kExecuteInput,
  kOpenMemoryExplorer,
  kExitShell,
};

// Result returned by LineEditor::ReadCommand.
struct LineEditorResult {
  LineEditorAction action = LineEditorAction::kExecuteInput;
  std::string input;
};

// Interactive VT100/ANSI line editor with syntax highlighting, ghost-text
// autocomplete, Tab completion menu, and Up/Down + mouse history block
// selection.
class LineEditor {
 public:
  explicit LineEditor(JsEngine& engine);
  ~LineEditor();

  // Reads a complete command (single-line slash command or potentially
  // multi-line JavaScript expression) or triggers a modal action.
  LineEditorResult ReadCommand(size_t turn_index);

 private:
  JsEngine& engine_;
  std::string prefill_buffer_;
};
