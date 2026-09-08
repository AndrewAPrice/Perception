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

#include "perception/ui/components/message_dialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"

using ::perception::Defer;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace perception {
namespace ui {
namespace components {
namespace {

// Default width for the message dialog in pixels.
constexpr float kMessageDialogWidth = 380.0f;

// Active message dialog instances.
std::vector<std::shared_ptr<Node>> active_dialogs;

void CloseDialog(std::shared_ptr<Node> window_node) {
  if (!window_node) return;
  if (auto ui_window = window_node->Get<UiWindow>()) ui_window->Close();
  Defer([window_node]() {
    auto it =
        std::find(active_dialogs.begin(), active_dialogs.end(), window_node);
    if (it != active_dialogs.end()) active_dialogs.erase(it);
  });
}

}  // namespace

void ShowMessageDialog(std::string_view title, std::string_view message,
                       std::shared_ptr<Node> parent_window,
                       std::function<void()> on_dismissed) {
  auto window_holder = std::make_shared<std::shared_ptr<Node>>();
  auto dismissed = std::make_shared<bool>(false);

  auto handle_close = [window_holder, dismissed, on_dismissed]() {
    if (*dismissed) return;
    *dismissed = true;
    if (on_dismissed) on_dismissed();
    CloseDialog(*window_holder);
  };

  auto dialog_window = UiWindow::DialogWithTitleBar(
      std::string(title), UiWindow::Parent(parent_window),
      [handle_close](UiWindow& window) {
        window.OnClose([handle_close]() { handle_close(); });
      },
      [](Layout& layout) { layout.SetWidth(kMessageDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel(std::string(message)),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetJustifyContent(YGJustifyFlexEnd);
              },
              Button::TextButton(
                  "OK", [handle_close]() { handle_close(); },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::PRIMARY);
                  }))));

  *window_holder = dialog_window;
  active_dialogs.push_back(dialog_window);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
