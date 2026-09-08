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

#include "format_dialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "perception/disk/disk_manager.h"
#include "perception/disk/filesystems.h"
#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/drop_down_box.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/message_dialog.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/theme.h"
#include "progress_dialog.h"

using ::perception::Defer;
using ::perception::disk::DiskInfo;
using ::perception::disk::DiskManager;
using ::perception::disk::FilesystemType;
using ::perception::disk::FilesystemTypeToString;
using ::perception::disk::GetWritableFilesystems;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kSecondaryTextColor;
using ::perception::ui::kWarningTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::DropDownBox;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ShowMessageDialog;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace dialogs {
namespace {

// Dialog width in pixels.
constexpr float kDialogWidth = 420.0f;

// Active format dialog instances.
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

void ShowFormatDialog(DiskManager& disk_manager, int disk_index,
                      int partition_number, std::shared_ptr<Node> parent_window,
                      std::function<void(bool success)> on_format_complete) {
  if (disk_index < 0) return;

  const auto& disks = disk_manager.GetDisks();
  if (disk_index >= static_cast<int>(disks.size())) return;

  auto label_holder = std::make_shared<std::string>("PERCEPTION");
  auto dialog_ptr = std::make_shared<std::shared_ptr<Node>>();

  const auto& writable_filesystems = GetWritableFilesystems();
  std::vector<std::string> fs_options;
  for (auto fs : writable_filesystems) {
    fs_options.push_back(std::string(FilesystemTypeToString(fs)));
  }
  auto selected_fs_idx = std::make_shared<int>(0);

  *dialog_ptr = UiWindow::DialogWithTitleBar(
      "Format and Erase", UiWindow::Parent(parent_window),
      [dialog_ptr](UiWindow& window) {
        window.OnClose([dialog_ptr]() { CloseDialog(*dialog_ptr); });
      },
      [](Layout& layout) { layout.SetWidth(kDialogWidth); },
      Container::VerticalContainer(
          Label::BasicLabel(
              "Volume Label:",
              [](Label& label) { label.SetFont(GetBold12UiFont()); }),
          InputBox::BasicInputBox(
              label_holder,
              [](Layout& layout) { layout.SetWidthPercent(100.0f); }),
          Label::BasicLabel(
              "Filesystem:",
              [](Label& label) { label.SetFont(GetBold12UiFont()); }),
          DropDownBox::BasicDropDownBox(
              fs_options, *selected_fs_idx,
              [selected_fs_idx](int new_index) {
                *selected_fs_idx = new_index;
              },
              [](Layout& layout) { layout.SetWidthPercent(100.0f); }),
          Label::BasicLabel("Warning: Formatting will permanently erase all "
                            "contents on this volume.",
                            [](Label& label) {
                              label.SetColor(kWarningTextColor);
                            }),
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
                  "Format",
                  [&disk_manager, disk_index, partition_number, label_holder,
                   selected_fs_idx, writable_filesystems, dialog_ptr,
                   parent_window, on_format_complete]() {
                    CloseDialog(*dialog_ptr);
                    std::string label = *label_holder;
                    FilesystemType selected_fs =
                        (*selected_fs_idx >= 0 &&
                         *selected_fs_idx <
                             static_cast<int>(writable_filesystems.size()))
                            ? writable_filesystems[*selected_fs_idx]
                            : FilesystemType::EXFAT;
                    std::string fs_name(FilesystemTypeToString(selected_fs));
                    auto progress_dialog = ShowProgressDialog(
                        "Formatting Volume",
                        "Formatting volume " + label + " with " + fs_name +
                            "...",
                        parent_window);

                    std::thread([&disk_manager, disk_index, partition_number,
                                 label, selected_fs, fs_name, progress_dialog,
                                 parent_window, on_format_complete]() {
                      auto& disk = const_cast<DiskInfo&>(
                          disk_manager.GetDisks()[disk_index]);
                      bool success = false;
                      if (partition_number > 0)
                        success = disk_manager.FormatPartition(
                            disk, partition_number, label, selected_fs);
                      else
                        success = disk_manager.FormatRawDisk(disk, label,
                                                             selected_fs);

                      Defer([progress_dialog, parent_window, on_format_complete,
                             success, fs_name]() {
                        CloseProgressDialog(progress_dialog);
                        if (!success)
                          ShowMessageDialog(
                              "Formatting Failed",
                              "Unable to format volume with " + fs_name +
                                  ". Make sure the drive is unmounted and "
                                  "writable.",
                              parent_window);

                        if (on_format_complete) on_format_complete(success);
                      });
                    }).detach();
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::PRIMARY);
                  }))));

  active_dialogs.push_back(*dialog_ptr);
}

}  // namespace dialogs
