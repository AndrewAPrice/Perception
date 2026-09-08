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

#include "change_mount_path_dialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "perception/disk/disk_manager.h"
#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"

using ::perception::Defer;
using ::perception::disk::DiskManager;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kWarningTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace dialogs {
namespace {

// Dialog width in pixels.
constexpr float kDialogWidth = 420.0f;

// Active change mount path dialog instances.
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

void ShowChangeMountPathDialog(DiskManager& disk_manager,
                               std::string_view current_mount_point,
                               std::shared_ptr<Node> parent_window,
                               std::function<void()> on_mount_path_changed) {
  std::string old_mount_path(current_mount_point);
  while (!old_mount_path.empty() && old_mount_path.front() == '/')
    old_mount_path.erase(old_mount_path.begin());
  while (!old_mount_path.empty() && old_mount_path.back() == '/')
    old_mount_path.pop_back();

  auto path_holder = std::make_shared<std::string>(old_mount_path);
  std::shared_ptr<Label> error_label;
  auto dialog_ptr = std::make_shared<std::shared_ptr<Node>>();

  *dialog_ptr = UiWindow::DialogWithTitleBar(
      "Change Mount Path", UiWindow::Parent(parent_window),
      [dialog_ptr](UiWindow& window) {
        window.OnClose([dialog_ptr]() { CloseDialog(*dialog_ptr); });
      },
      [](Layout& layout) { layout.SetWidth(kDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel("Current mount point: /" + old_mount_path),
          Label::BasicLabel(
              "New Mount Name:",
              [](Label& label) { label.SetFont(GetBold12UiFont()); }),
          InputBox::BasicInputBox(
              path_holder,
              [](Layout& layout) { layout.SetWidthPercent(100.0f); }),
          Label::BasicLabel(
              " ",
              [](Label& label) {
                label.SetColor(kWarningTextColor);
              },
              &error_label),
          Node::Empty([](Layout& layout) { layout.SetFlexGrow(1.0f); }),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetJustifyContent(YGJustifyFlexEnd);
              },
              Button::TextButton(
                  "Cancel", [dialog_ptr]() { CloseDialog(*dialog_ptr); },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::SECONDARY);
                  }),
              Button::TextButton(
                  "Apply",
                  [&disk_manager, old_mount_path, path_holder, error_label,
                   dialog_ptr, on_mount_path_changed]() {
                    std::string new_name = *path_holder;
                    while (!new_name.empty() && new_name.front() == '/')
                      new_name.erase(new_name.begin());
                    while (!new_name.empty() && new_name.back() == '/')
                      new_name.pop_back();

                    if (new_name.empty()) {
                      error_label->SetText("Mount name cannot be blank.");
                      return;
                    } else if (new_name.find('/') != std::string::npos) {
                      error_label->SetText(
                          "Mount name cannot contain slashes.");
                      return;
                    } else if (new_name == "Applications" ||
                               new_name == "Libraries") {
                      error_label->SetText("Mount name \"" + new_name +
                                           "\" is reserved.");
                      return;
                    }

                    bool success =
                        disk_manager.SetMountPath(old_mount_path, new_name);
                    if (!success) {
                      error_label->SetText(
                          "Failed to set mount path (name may already "
                          "be used).");
                      return;
                    }

                    CloseDialog(*dialog_ptr);
                    if (on_mount_path_changed) on_mount_path_changed();
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::PRIMARY);
                  }))));

  active_dialogs.push_back(*dialog_ptr);
}

}  // namespace dialogs
