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

#include "dialogs.h"

#include <filesystem>

#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"

using ::perception::Defer;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace {

// Default modal dialog width.
constexpr float kDialogWidth = 340.0f;

std::shared_ptr<Node> active_dialog;

}  // namespace

void CloseActiveDialog() {
  if (!active_dialog) return;
  if (auto ui_window = active_dialog->Get<UiWindow>()) ui_window->Close();
  active_dialog.reset();
}

void ShowTextInputDialog(std::string_view title, std::string_view prompt,
                         std::string_view initial_value,
                         std::string_view confirm_label,
                         std::shared_ptr<Node> parent_window,
                         std::function<void(std::string)> on_confirm,
                         std::function<void()> on_closed) {
  CloseActiveDialog();

  auto submitted = std::make_shared<bool>(false);
  auto input_box_node = InputBox::BasicInputBox(
      initial_value, [](Layout& layout) { layout.SetWidthPercent(100.0f); });

  auto submit = [submitted, input_box_node, on_confirm, on_closed]() {
    if (*submitted) return;
    std::string value;
    if (input_box_node) {
      if (auto input = input_box_node->Get<InputBox>())
        value = input->GetText();
    }
    if (value.empty() || value.find('/') != std::string::npos) return;
    *submitted = true;
    Defer([value, on_confirm, on_closed]() {
      CloseActiveDialog();
      if (on_confirm) on_confirm(value);
      if (on_closed) on_closed();
    });
  };

  if (auto input = input_box_node->Get<InputBox>())
    input->OnEnterPressed([submit](std::string_view) { submit(); });

  active_dialog = UiWindow::DialogWithTitleBar(
      title, UiWindow::Parent(parent_window),
      [on_closed](UiWindow& win) {
        win.OnClose([on_closed]() {
          active_dialog.reset();
          if (on_closed) on_closed();
        });
      },
      [](Layout& layout) { layout.SetWidth(kDialogWidth); },
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetAlignItems(YGAlignStretch); },
          Label::BasicLabel(prompt), input_box_node,
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetJustifyContent(YGJustifyFlexEnd);
              },
              Button::TextButton("Cancel",
                                 [on_closed]() {
                                   CloseActiveDialog();
                                   if (on_closed) on_closed();
                                 }),
              Button::TextButton(
                  confirm_label, [submit]() { submit(); },
                  [](Button& btn) {
                    btn.SetButtonStyle(ButtonStyle::PRIMARY);
                  }))));

  if (auto input = input_box_node->Get<InputBox>()) {
    input->Focus();
    input->SelectAll();
  }
}

void ShowDeleteConfirmationDialog(const std::vector<std::string>& targets,
                                  std::shared_ptr<Node> parent_window,
                                  std::function<void()> on_confirm,
                                  std::function<void()> on_closed) {
  if (targets.empty()) return;
  CloseActiveDialog();

  std::string message;
  if (targets.size() == 1) {
    std::string name = std::filesystem::path(targets[0]).filename().string();
    message = "Are you sure you want to delete \"" + name + "\"?";
  } else {
    message = "Are you sure you want to delete " +
              std::to_string(targets.size()) + " selected items?";
  }

  active_dialog = UiWindow::DialogWithTitleBar(
      "Delete", UiWindow::Parent(parent_window),
      [on_closed](UiWindow& win) {
        win.OnClose([on_closed]() {
          active_dialog.reset();
          if (on_closed) on_closed();
        });
      },
      [](Layout& layout) { layout.SetWidth(kDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel(message),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetJustifyContent(YGJustifyFlexEnd);
              },
              Button::TextButton("Cancel",
                                 [on_closed]() {
                                   CloseActiveDialog();
                                   if (on_closed) on_closed();
                                 }),
              Button::TextButton(
                  "Delete",
                  [on_confirm, on_closed]() {
                    CloseActiveDialog();
                    if (on_confirm) on_confirm();
                    if (on_closed) on_closed();
                  },
                  [](Button& btn) {
                    btn.SetButtonStyle(ButtonStyle::RED);
                  }))));
}
