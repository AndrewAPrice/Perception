// Copyright 2026
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

#include <exception>
#include <memory>
#include <string>
#include <string_view>

#include "inspected_node.h"
#include "nlohmann/json.hpp"
#include "perception/processes.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "ui_debugger_window.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::Node;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;
using json = ::nlohmann::json;

namespace {

std::unique_ptr<UIDebuggerWindow> active_window;
std::shared_ptr<Node> error_dialog;

void CreateErrorDialog(std::string_view message) {
  error_dialog = UiWindow::DialogWithTitleBar(
      "UI Debugger Error",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Label::BasicLabel(message));
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string_view initial_json = argc > 1 ? argv[1] : "";
  std::shared_ptr<InspectedNode> root;
  if (initial_json.empty()) {
    CreateErrorDialog("No view hierarchy JSON was provided to inspect.");
  } else {
    try {
      json data = json::parse(initial_json);
      root = ParseInspectedNode(data, 0, ::perception::ui::Point{0, 0},
                                ::perception::ui::Point{0, 0});
    } catch (std::exception& e) {
      CreateErrorDialog("Invalid view hierarchy JSON provided: " +
                        std::string(e.what()));
    }
  }

  if (!root) return -1;

  ::perception::window::BaseWindow::Client live_window;
  if (argc > 3) {
    try {
      live_window = ::perception::window::BaseWindow::Client(
          std::stoull(argv[2]), std::stoull(argv[3]));
    } catch (...) {
    }
  }
  active_window = std::make_unique<UIDebuggerWindow>(root, live_window);

  HandOverControl();
  return 0;
}
