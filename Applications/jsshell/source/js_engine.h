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
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "completion.h"
#include "perception/shared_memory_pipe.h"
#include "quickjs.h"
#include "types.h"

// Type of history entry recorded during an interactive REPL session.
enum class HistoryBlockKind {
  kQuery,
  kResponse,
};

// A query or response block tracked in the REPL session history.
struct HistoryBlock {
  HistoryBlockKind kind = HistoryBlockKind::kQuery;
  size_t turn_index = 0;
  std::string text;
  std::vector<std::string> rendered_lines;
  JSValue js_value = JS_UNDEFINED;
  bool has_js_value = false;
  int start_screen_row = -1;
  int end_screen_row = -1;
};

// Metadata for a background process started via `.bg()`.
struct BackgroundJob {
  size_t job_id = 0;
  perception::ProcessId pid = 0;
  std::string name;
  std::string command_line;
  bool running = true;
};

// Owns the QuickJS runtime, context, built-in namespaces, history blocks,
// terminal state, and asynchronous evaluation loop.
class JsEngine {
 public:
  JsEngine();
  ~JsEngine();

  JsEngine(const JsEngine&) = delete;
  JsEngine& operator=(const JsEngine&) = delete;

  // Resets the entire QuickJS session, freeing all history JSValues and
  // creating a fresh JSContext with clean built-in namespaces.
  void ResetSession();

  // Populates `sys.args` with script arguments.
  void SetScriptArgs(const std::vector<std::string>& args);

  // Returns the current script arguments (`sys.args`).
  const std::vector<std::string>& ScriptArgs() const { return script_args_; }

  // Evaluates a JavaScript snippet asynchronously, drains all pending
  // microtasks and foreground pipelines, and returns the settled JSValue
  // (caller must JS_FreeValue). Sets `had_exception` if an error occurred.
  JSValue EvaluateAsync(std::string_view code, std::string_view filename,
                        bool is_repl, bool& had_exception);

  // Evaluates a script file from disk and drains all pending microtasks.
  bool RunScriptFile(std::string_view path,
                     const std::vector<std::string>& args);

  // Evaluates inline `--script` code, prints any non-undefined result, and
  // drains all pending microtasks.
  bool RunInlineScript(std::string_view code);

  // Formats and prints a settled REPL return value according to type rules
  // (undefined prints nothing; string prints directly; objects/primitives print
  // formatted previews), updates `_`, and records a ResponseBlock if non-void.
  void HandleReplReturnValue(JSValue val, size_t turn_index);

  // Executes a REPL slash command. Returns false if `/exit` was invoked.
  bool ExecuteSlashCommand(std::string_view line, size_t turn_index,
                           bool& open_memory_explorer);

  // Drains all pending QuickJS jobs/microtasks.
  void DrainMicrotasks();

  // Waits for a JSValue (unwrapping Promises and Command/Pipeline thenables)
  // while pumping QuickJS microtasks and Perception fibers.
  JSValue AwaitValue(JSValue val, bool& had_exception);

  // Queries completion candidates for the given input buffer and cursor offset.
  std::vector<CompletionItem> GetCompletions(std::string_view line,
                                             size_t cursor_pos);

  // Formats a JSValue as a syntax-highlighted or plain string for previewing.
  std::string FormatJsValuePreview(JSValue val, bool colorize,
                                   int max_lines = 12);

  // Requests interruption of running JS evaluation or foreground waits.
  void RequestInterrupt() { interrupt_requested_ = true; }

  // Clears any pending interrupt request flag.
  void ClearInterrupt() { interrupt_requested_ = false; }

  // Returns true if Ctrl+C interrupt was requested.
  bool IsInterruptRequested() const { return interrupt_requested_; }

  // Restores terminal modes (altScreen, rawMode, cursor, mouse tracking) if a
  // script left them modified.
  void RestoreTerminalState();

  // Returns the underlying QuickJS runtime.
  JSRuntime* Runtime() const { return rt_; }

  // Returns the underlying QuickJS context.
  JSContext* Context() const { return ctx_; }

  // Returns the current working directory.
  const std::string& Cwd() const { return cwd_; }

  // Sets the current working directory (normalizing and verifying existence).
  bool SetCwd(std::string_view path, std::string& error_out);

  // Returns the mutable list of session history blocks.
  std::vector<HistoryBlock>& History() { return history_; }
  const std::vector<HistoryBlock>& History() const { return history_; }

  // Records a query block in the session history and returns its index.
  size_t RecordQueryBlock(std::string_view text, size_t turn_index);

  // Shifts recorded screen row coordinates of visible history blocks upward by
  // `lines_scrolled` when the terminal scrolls.
  void NotifyTerminalScrolled(int lines_scrolled, int screen_rows);

  // Clears screen coordinates on all history blocks (e.g. on `/clear`).
  void ClearHistoryScreenCoordinates();
  void ClearVisibleHistory() { ClearHistoryScreenCoordinates(); }

  // Queries the terminal cursor position via DSR (`\x1b[6n`). Returns true and
  // sets `row` and `col` (1-based) if the terminal responds.
  bool QueryCursorPosition(int& row, int& col);

  // queries the terminal size in rows and columns.
  void QueryTerminalSize(int& cols, int& rows);

  // Tracks whether user scripts have enabled alternate screen or raw mode.
  void SetScriptAltScreen(bool enabled) { script_alt_screen_ = enabled; }
  void SetScriptRawMode(bool enabled) { script_raw_mode_ = enabled; }
  bool IsScriptRawMode() const { return script_raw_mode_; }
  void SetScriptCursorVisible(bool visible) {
    script_cursor_visible_ = visible;
  }

  // Registers a background job and returns its job ID.
  size_t AddBackgroundJob(perception::ProcessId pid, std::string_view name,
                          std::string_view command_line);

  // Refreshes and returns the list of background jobs.
  const std::vector<BackgroundJob>& GetBackgroundJobs();

 private:
  void InitializeContext();
  void FreeHistoryValues();

  JSRuntime* rt_ = nullptr;
  JSContext* ctx_ = nullptr;
  std::string cwd_ = "/";
  std::vector<std::string> script_args_;
  std::vector<HistoryBlock> history_;
  std::vector<BackgroundJob> jobs_;
  size_t next_job_id_ = 1;
  bool interrupt_requested_ = false;
  bool script_alt_screen_ = false;
  bool script_raw_mode_ = false;
  bool script_cursor_visible_ = true;
};

// Retrieves the JsEngine instance associated with a JSContext.
JsEngine* GetJsEngine(JSContext* ctx);
