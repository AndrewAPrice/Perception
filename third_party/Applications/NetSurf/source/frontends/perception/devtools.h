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
#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/node.h"

extern "C" {
#include "netsurf/console.h"
}

namespace netsurf {
namespace perception {

class Window;

// Available panels in the Developer Tools inspector.
enum class DevToolsPanel {
  Console = 0,
  Source = 1,
  Resources = 2,
  Network = 3,
  Document = 4,
  PageInfo = 5,
};

// Severity levels for browser console log messages.
enum class ConsoleLevel {
  Debug,
  Log,
  Info,
  Warn,
  Error,
};

// A single entry in a tab's console log buffer.
struct ConsoleMessage {
  browser_window_console_source source = BW_CS_SCRIPT_CONSOLE;
  ConsoleLevel level = ConsoleLevel::Log;
  bool foldable = false;
  std::string text;
  int64_t timestamp_ms = 0;
};

// Summary counts of errors and warnings logged for a tab.
struct ConsoleCounts {
  size_t error_count = 0;
  size_t warning_count = 0;
};

// Appends a console log message for a browser tab and notifies listeners.
void AppendConsoleMessage(Window* gw,
                          browser_window_console_source src,
                          const char* msg,
                          size_t msglen,
                          browser_window_console_flags flags);

// Clears console messages for the specified tab unless preserve log is enabled
// and force is false.
void ClearConsoleMessagesForTab(Window* gw, bool force = false);

// Returns the error and warning counts for the specified tab.
ConsoleCounts GetConsoleCountsForTab(Window* gw);

// Registers a listener invoked whenever console messages change.
uint64_t AddConsoleListener(std::function<void()> on_changed);

// Unregisters a previously registered console listener.
void RemoveConsoleListener(uint64_t listener_id);

// Opens or switches Developer Tools to the specified panel for the active tab.
void OpenDevTools(DevToolsPanel panel = DevToolsPanel::Console);

// Toggles Developer Tools open or closed on the specified panel.
void ToggleDevTools(DevToolsPanel panel = DevToolsPanel::Console);

// Closes Developer Tools.
void CloseDevTools();

// Returns true if Developer Tools is currently open.
bool IsDevToolsOpen();

// Returns true if Developer Tools is docked in the browser window rather than
// popped out into a separate window.
bool IsDevToolsDocked();

// Sets whether Developer Tools is docked in the browser window or popped out
// into a separate window.
void SetDevToolsDocked(bool docked);

// Returns the docked Developer Tools UI node to be placed in the browser
// window's split container when docked.
std::shared_ptr<::perception::ui::Node> GetDockedDevToolsNode();

// Sets a callback invoked whenever Developer Tools open/close or dock/undock
// state changes so the browser window can update its split layout.
void SetDevToolsLayoutCallback(std::function<void()> callback);

// Notifies Developer Tools that the active tab changed.
void NotifyDevToolsTabChanged();

// Notifies Developer Tools that a tab finished loading a page.
void NotifyDevToolsPageLoaded(Window* gw);

// Notifies Developer Tools that a tab started navigating to a new page.
void NotifyDevToolsNavigationStarted(Window* gw);

// Opens Developer Tools on the Resources panel and selects the matching URL.
void InspectResourceUrl(std::string_view url);

}  // namespace perception
}  // namespace netsurf
