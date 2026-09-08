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

#include "partition_disk_dialog.h"

#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "progress_dialog.h"
#include "perception/disk/disk_manager.h"
#include "perception/disk/filesystems.h"
#include "perception/file.h"
#include "perception/scheduler.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/drop_down_box.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/message_dialog.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"

using ::perception::Defer;
using ::perception::FormatSize;
using ::perception::disk::DiskInfo;
using ::perception::disk::DiskManager;
using ::perception::disk::FilesystemType;
using ::perception::disk::FilesystemTypeToString;
using ::perception::disk::GetWritableFilesystems;
using ::perception::disk::PartitionInfo;
using ::perception::disk::PartitionScheme;
using ::perception::disk::PlannedPartition;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::kNoticeTextColor;
using ::perception::ui::kSecondaryTextColor;
using ::perception::ui::kSuccessTextColor;
using ::perception::ui::kWarningTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::DropDownBox;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::ShowMessageDialog;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;

namespace dialogs {
namespace {

// Partition disk dialog width in pixels.
constexpr float kPartitionDiskDialogWidth = 560.0f;

// Partition disk dialog height in pixels.
constexpr float kPartitionDiskDialogHeight = 480.0f;

// Maximum number of primary partitions supported by MBR.
constexpr size_t kMaxMbrPartitions = 4;

// Maximum number of partitions supported by GPT.
constexpr size_t kMaxGptPartitions = 128;

// Partition scheme dropdown index for GUID Partition Table.
constexpr int kGptSchemeIndex = 0;

// Partition scheme dropdown index for Master Boot Record.
constexpr int kMbrSchemeIndex = 1;

// Partition scheme dropdown index for Unpartitioned / Superfloppy.
constexpr int kSuperfloppySchemeIndex = 2;

// Active partition disk dialog instances.
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

void ShowPartitionDiskDialog(
    DiskManager& disk_manager, int disk_index,
    std::shared_ptr<Node> parent_window,
    std::function<void(bool success)> on_partition_applied) {
  if (disk_index < 0) return;

  const auto& disks = disk_manager.GetDisks();
  if (disk_index >= static_cast<int>(disks.size())) return;

  const auto& disk = disks[disk_index];

  int initial_scheme_idx = kGptSchemeIndex;
  if (disk.scheme == PartitionScheme::MBR)
    initial_scheme_idx = kMbrSchemeIndex;
  else if (disk.scheme == PartitionScheme::NONE)
    initial_scheme_idx = kSuperfloppySchemeIndex;

  const auto& writable_filesystems = GetWritableFilesystems();
  std::vector<std::string> fs_options;
  for (auto fs : writable_filesystems) {
    fs_options.push_back(std::string(FilesystemTypeToString(fs)));
  }
  FilesystemType default_fs = writable_filesystems.empty()
                                  ? FilesystemType::EXFAT
                                  : writable_filesystems[0];
  auto selected_raw_fs_idx = std::make_shared<int>(0);

  auto selected_scheme_idx = std::make_shared<int>(initial_scheme_idx);
  auto raw_volume_label = std::make_shared<std::string>("PERCEPTION");
  auto planned_partitions = std::make_shared<std::vector<PlannedPartition>>();

  uint64_t usable_bytes = disk.size_in_bytes > 2 * 1024 * 1024
                              ? disk.size_in_bytes - 2 * 1024 * 1024
                              : disk.size_in_bytes;
  uint64_t total_usable_mb = usable_bytes / (1024 * 1024);

  if (!disk.partitions.empty() && disk.scheme != PartitionScheme::NONE) {
    for (const auto& p : disk.partitions) {
      PlannedPartition plan;
      plan.original_partition_number = p.partition_number;
      plan.original_start_lba = p.start_lba;
      plan.original_sector_count = p.sector_count;
      plan.name = p.name;
      plan.size_in_bytes = p.size_in_bytes;
      plan.filesystem_type = p.filesystem_type != FilesystemType::UNKNOWN &&
                             p.filesystem_type != FilesystemType::RAW
                                 ? p.filesystem_type
                                 : default_fs;
      plan.is_untouched = true;
      planned_partitions->push_back(plan);
    }
  } else {
    PlannedPartition default_part;
    default_part.original_partition_number = -1;
    default_part.name = "Volume 1";
    default_part.size_in_bytes = usable_bytes;
    default_part.filesystem_type = default_fs;
    default_part.is_untouched = false;
    planned_partitions->push_back(default_part);
  }

  std::vector<std::string> scheme_options = {"GUID Partition Table (GPT)",
                                             "Master Boot Record (MBR)",
                                             "Unpartitioned / Superfloppy"};

  auto dialog_ptr = std::make_shared<std::shared_ptr<Node>>();
  auto dynamic_content_container = std::make_shared<std::shared_ptr<Node>>();

  auto free_space_label_node = std::make_shared<std::shared_ptr<Node>>();
  auto add_partition_button_node = std::make_shared<std::shared_ptr<Node>>();
  auto apply_button_node = std::make_shared<std::shared_ptr<Node>>();
  auto partition_status_nodes =
      std::make_shared<std::vector<std::shared_ptr<Node>>>();

  auto update_metrics_and_buttons = std::make_shared<std::function<void()>>();
  *update_metrics_and_buttons = [&disk_manager, disk_index, selected_scheme_idx,
                                 planned_partitions, total_usable_mb,
                                 free_space_label_node,
                                 add_partition_button_node, apply_button_node,
                                 partition_status_nodes]() {
    const auto& disk_ref = disk_manager.GetDisks()[disk_index];
    PartitionScheme target_scheme = PartitionScheme::GPT;
    if (*selected_scheme_idx == kMbrSchemeIndex)
      target_scheme = PartitionScheme::MBR;
    else if (*selected_scheme_idx == kSuperfloppySchemeIndex)
      target_scheme = PartitionScheme::NONE;

    if (target_scheme == PartitionScheme::NONE) {
      if (*apply_button_node) {
        (*apply_button_node)
            ->Get<Button>()
            ->SetButtonStyle(ButtonStyle::PRIMARY);
        (*apply_button_node)->Invalidate();
      }
      return;
    }

    int64_t total_planned_mb = 0;
    for (const auto& p : *planned_partitions)
      total_planned_mb += p.size_in_bytes / (1024 * 1024);

    int64_t available_mb =
        static_cast<int64_t>(total_usable_mb) - total_planned_mb;

    if (*free_space_label_node) {
      auto label_cmp = (*free_space_label_node)->Get<Label>();
      if (available_mb < 0) {
        uint64_t deficit_bytes =
            static_cast<uint64_t>(-available_mb) * 1024 * 1024;
        label_cmp->SetText("Free Space: -" + FormatSize(deficit_bytes) +
                           " (Exceeds capacity by " +
                           FormatSize(deficit_bytes) + ")");
        label_cmp->SetColor(kWarningTextColor);
      } else {
        label_cmp->SetText(
            "Available Free Space: " +
            FormatSize(static_cast<uint64_t>(available_mb) * 1024 * 1024) +
            " of " + FormatSize(total_usable_mb * 1024 * 1024));
        label_cmp->SetColor(kSecondaryTextColor);
      }
      (*free_space_label_node)->Invalidate();
    }

    size_t max_parts = (target_scheme == PartitionScheme::MBR)
                           ? kMaxMbrPartitions
                           : kMaxGptPartitions;
    bool can_add = (available_mb > 0 && planned_partitions->size() < max_parts);
    if (*add_partition_button_node) {
      (*add_partition_button_node)
          ->Get<Button>()
          ->SetButtonStyle(can_add ? ButtonStyle::SECONDARY
                                   : ButtonStyle::DISABLED);
      (*add_partition_button_node)->Invalidate();
    }

    bool can_apply = (available_mb >= 0 && !planned_partitions->empty());
    if (*apply_button_node) {
      (*apply_button_node)
          ->Get<Button>()
          ->SetButtonStyle(can_apply ? ButtonStyle::PRIMARY
                                     : ButtonStyle::DISABLED);
      (*apply_button_node)->Invalidate();
    }

    bool scheme_matches = (target_scheme == disk_ref.scheme);
    for (size_t i = 0; i < planned_partitions->size(); i++) {
      auto& p = (*planned_partitions)[i];
      if (!scheme_matches || p.original_partition_number <= 0) {
        p.is_untouched = false;
      } else {
        auto orig_itr = std::find_if(
            disk_ref.partitions.begin(), disk_ref.partitions.end(),
            [&p](const PartitionInfo& orig) {
              return orig.partition_number == p.original_partition_number;
            });
        if (orig_itr != disk_ref.partitions.end() &&
            orig_itr->size_in_bytes == p.size_in_bytes &&
            orig_itr->filesystem_type == p.filesystem_type) {
          p.is_untouched = true;
        } else {
          p.is_untouched = false;
        }
      }

      if (i < partition_status_nodes->size() && (*partition_status_nodes)[i]) {
        auto status_node = (*partition_status_nodes)[i];
        auto label_cmp = status_node->Get<Label>();
        if (p.is_untouched) {
          label_cmp->SetText("✓");
          label_cmp->SetColor(kSuccessTextColor);
          Tooltip::Attach(status_node, "Data will be untouched");
        } else {
          label_cmp->SetText("⚠️");
          label_cmp->SetColor(kNoticeTextColor);
          Tooltip::Attach(status_node, "Partition will be erased");
        }
        status_node->Invalidate();
      }
    }
  };

  auto update_dialog_content = std::make_shared<std::function<void()>>();
  *update_dialog_content = [&disk_manager, disk_index, selected_scheme_idx,
                            raw_volume_label, planned_partitions,
                            dynamic_content_container, total_usable_mb,
                            free_space_label_node, add_partition_button_node,
                            apply_button_node, partition_status_nodes,
                            update_dialog_content,
                            update_metrics_and_buttons,
                            selected_raw_fs_idx, fs_options,
                            writable_filesystems, default_fs]() {
    if (!*dynamic_content_container) return;

    auto container = *dynamic_content_container;
    container->RemoveChildren();
    partition_status_nodes->clear();

    PartitionScheme target_scheme = PartitionScheme::GPT;
    if (*selected_scheme_idx == kMbrSchemeIndex)
      target_scheme = PartitionScheme::MBR;
    else if (*selected_scheme_idx == kSuperfloppySchemeIndex)
      target_scheme = PartitionScheme::NONE;

    if (target_scheme == PartitionScheme::NONE) {
      container->AddChild(Label::BasicLabel("Volume Label:", [](Label& label) {
        label.SetFont(GetBold12UiFont());
      }));

      container->AddChild(InputBox::BasicInputBox(
          raw_volume_label,
          [](Layout& layout) { layout.SetWidthPercent(100.0f); }));

      container->AddChild(Label::BasicLabel("Filesystem:", [](Label& label) {
        label.SetFont(GetBold12UiFont());
      }));

      container->AddChild(DropDownBox::BasicDropDownBox(
          fs_options, *selected_raw_fs_idx,
          [selected_raw_fs_idx](int new_index) {
            *selected_raw_fs_idx = new_index;
          },
          [](Layout& layout) { layout.SetWidthPercent(100.0f); }));

      container->AddChild(
          Label::BasicLabel("Note: Formats the entire storage device as a "
                            "single filesystem without a partition table.",
                            [](Label& label) {
                              label.SetColor(kSecondaryTextColor);
                            }));

      container->AddChild(Label::BasicLabel(
          "⚠️ Warning: Formatting will remove all data on this disk.",
          [](Label& label) {
            label.SetColor(kWarningTextColor);
          }));

    } else {
      container->AddChild(Label::BasicLabel("Partitions:", [](Label& label) {
        label.SetFont(GetBold12UiFont());
      }));

      auto list_card = Container::VerticalContainer([](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
      });

      for (size_t i = 0; i < planned_partitions->size(); i++) {
        size_t part_idx = i;
        auto& p = (*planned_partitions)[part_idx];
        uint64_t part_mb = p.size_in_bytes / (1024 * 1024);

        auto row = Container::HorizontalContainer([](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetAlignItems(YGAlignCenter);
        });

        auto status_label = Label::BasicLabel(
            p.is_untouched ? "✓" : "⚠️",
            [](Layout& layout) {
              layout.SetWidth(16.0f);
              layout.SetFlexShrink(0.0f);
            },
            [is_untouched = p.is_untouched](Label& label) {
              label.SetColor(is_untouched ? kSuccessTextColor : kNoticeTextColor);
              label.SetFont(GetBold12UiFont());
              label.SetTextAlignment(TextAlignment::MiddleCenter);
            },
            Tooltip::ShowTooltip(p.is_untouched ? "Data will be untouched"
                                                : "Partition will be erased"));

        partition_status_nodes->push_back(status_label);
        row->AddChild(status_label);

        row->AddChild(Label::BasicLabel("Name:", [](Label& label) {
          label.SetColor(kSecondaryTextColor);
        }));
        row->AddChild(InputBox::BasicInputBox(
            p.name,
            [planned_partitions, part_idx](InputBox& input) {
              input.OnTextChanged(
                  [planned_partitions, part_idx](std::string_view text) {
                    if (part_idx < planned_partitions->size())
                      (*planned_partitions)[part_idx].name = std::string(text);
                  });
              input.OnEnterPressed(
                  [planned_partitions, part_idx](std::string_view text) {
                    if (part_idx < planned_partitions->size())
                      (*planned_partitions)[part_idx].name = std::string(text);
                  });
            },
            [](Layout& layout) {
              layout.SetWidth(110.0f);
              layout.SetFlexShrink(1.0f);
            }));

        row->AddChild(Label::BasicLabel("Size:", [](Label& label) {
          label.SetColor(kSecondaryTextColor);
        }));
        row->AddChild(InputBox::BasicInputBox(
            std::to_string(part_mb),
            [planned_partitions, part_idx,
             update_metrics_and_buttons](InputBox& input) {
              input.OnTextChanged(
                  [planned_partitions, part_idx,
                   update_metrics_and_buttons](std::string_view text) {
                    if (part_idx < planned_partitions->size()) {
                      try {
                        if (!text.empty()) {
                          uint64_t new_mb = std::stoull(std::string(text));
                          (*planned_partitions)[part_idx].size_in_bytes =
                              new_mb * 1024 * 1024;
                        } else {
                          (*planned_partitions)[part_idx].size_in_bytes = 0;
                        }
                      } catch (...) {
                      }
                      (*update_metrics_and_buttons)();
                    }
                  });
              input.OnEnterPressed(
                  [planned_partitions, part_idx,
                   update_metrics_and_buttons](std::string_view text) {
                    if (part_idx < planned_partitions->size()) {
                      try {
                        if (!text.empty()) {
                          uint64_t new_mb = std::stoull(std::string(text));
                          (*planned_partitions)[part_idx].size_in_bytes =
                              new_mb * 1024 * 1024;
                        } else {
                          (*planned_partitions)[part_idx].size_in_bytes = 0;
                        }
                      } catch (...) {
                      }
                      (*update_metrics_and_buttons)();
                    }
                  });
            },
            [](Layout& layout) {
              layout.SetWidth(80.0f);
              layout.SetFlexShrink(0.0f);
            }));
        row->AddChild(Label::BasicLabel("MB", [](Label& label) {
          label.SetColor(kSecondaryTextColor);
        }));

        int fs_selection_idx = 0;
        for (size_t f = 0; f < writable_filesystems.size(); f++) {
          if (writable_filesystems[f] == p.filesystem_type) {
            fs_selection_idx = static_cast<int>(f);
            break;
          }
        }

        row->AddChild(DropDownBox::BasicDropDownBox(
            fs_options, fs_selection_idx,
            [planned_partitions, part_idx, writable_filesystems,
             update_metrics_and_buttons](int new_index) {
              if (part_idx < planned_partitions->size() && new_index >= 0 &&
                  new_index < static_cast<int>(writable_filesystems.size())) {
                auto& part = (*planned_partitions)[part_idx];
                if (part.filesystem_type != writable_filesystems[new_index]) {
                  part.filesystem_type = writable_filesystems[new_index];
                  part.is_untouched = false;
                }
                (*update_metrics_and_buttons)();
              }
            },
            [](Layout& layout) {
              layout.SetWidth(90.0f);
              layout.SetFlexShrink(0.0f);
            }));

        row->AddChild(Node::Empty([](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
        }));

        if (planned_partitions->size() > 1) {
          row->AddChild(Button::TextButton(
              "Remove",
              [planned_partitions, part_idx, update_dialog_content]() {
                if (part_idx < planned_partitions->size()) {
                  planned_partitions->erase(planned_partitions->begin() +
                                            part_idx);
                  Defer([update_dialog_content]() {
                    (*update_dialog_content)();
                  });
                }
              },
              [](Button& button) {
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              }));
        }

        list_card->AddChild(row);
      }

      container->AddChild(ScrollContainer::VerticalScrollContainer(
          list_card, [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
          }));

      auto add_btn = Button::TextButton(
          "+ Add Partition",
          [planned_partitions, target_scheme, total_usable_mb, default_fs,
           update_dialog_content]() {
            size_t max_parts = (target_scheme == PartitionScheme::MBR)
                                   ? kMaxMbrPartitions
                                   : kMaxGptPartitions;
            int64_t total_mb = 0;
            for (const auto& p : *planned_partitions)
              total_mb += p.size_in_bytes / (1024 * 1024);
            int64_t free_mb = static_cast<int64_t>(total_usable_mb) - total_mb;
            if (free_mb > 0 && planned_partitions->size() < max_parts) {
              PlannedPartition new_p;
              new_p.original_partition_number = -1;
              new_p.name =
                  "Partition " + std::to_string(planned_partitions->size() + 1);
              new_p.size_in_bytes =
                  static_cast<uint64_t>(free_mb) * 1024 * 1024;
              new_p.filesystem_type = default_fs;
              new_p.is_untouched = false;
              planned_partitions->push_back(new_p);
              Defer([update_dialog_content]() { (*update_dialog_content)(); });
            }
          },
          [](Button& button) {
            button.SetButtonStyle(ButtonStyle::SECONDARY);
          });
      *add_partition_button_node = add_btn;

      auto free_label =
          Label::BasicLabel("Available Free Space", [](Label& label) {
            label.SetColor(kSecondaryTextColor);
          });
      *free_space_label_node = free_label;

      auto controls_row = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
          },
          add_btn, free_label);

      container->AddChild(controls_row);

      container->AddChild(
          Label::BasicLabel("⚠️ Note: Changing the partition scheme, removing a "
                            "partition, changing the filesystem, or resizing a "
                            "partition will remove all data on it.",
                            [](Label& label) {
                              label.SetColor(kNoticeTextColor);
                            }));
    }

    container->Invalidate();
    (*update_metrics_and_buttons)();
  };

  auto apply_btn = Button::TextButton(
      "Apply & Partition",
      [&disk_manager, disk_index, selected_scheme_idx, raw_volume_label,
       selected_raw_fs_idx, writable_filesystems, default_fs,
       planned_partitions, total_usable_mb, dialog_ptr, parent_window,
       on_partition_applied]() {
        PartitionScheme scheme = PartitionScheme::GPT;
        if (*selected_scheme_idx == kMbrSchemeIndex)
          scheme = PartitionScheme::MBR;
        else if (*selected_scheme_idx == kSuperfloppySchemeIndex)
          scheme = PartitionScheme::NONE;

        if (scheme != PartitionScheme::NONE) {
          int64_t total_mb = 0;
          for (const auto& p : *planned_partitions)
            total_mb += p.size_in_bytes / (1024 * 1024);
          if (static_cast<int64_t>(total_usable_mb) - total_mb < 0 ||
              planned_partitions->empty())
            return;
        }

        CloseDialog(*dialog_ptr);
        const auto& disks = disk_manager.GetDisks();
        std::string disk_name = (disk_index < static_cast<int>(disks.size()))
                                    ? disks[disk_index].name
                                    : "Disk";

        auto progress_dialog = ShowProgressDialog(
            "Partitioning Disk",
            "Applying partition layout to " + disk_name + "...", parent_window);

        std::vector<PlannedPartition> parts_to_apply;
        if (scheme == PartitionScheme::NONE) {
          PlannedPartition raw_part;
          raw_part.name = *raw_volume_label;
          raw_part.filesystem_type =
              (*selected_raw_fs_idx >= 0 &&
               *selected_raw_fs_idx <
                   static_cast<int>(writable_filesystems.size()))
                  ? writable_filesystems[*selected_raw_fs_idx]
                  : default_fs;
          parts_to_apply.push_back(raw_part);
        } else {
          parts_to_apply = *planned_partitions;
        }

        std::thread([&disk_manager, disk_index, disk_name, scheme,
                     parts_to_apply, progress_dialog, parent_window,
                     on_partition_applied]() {
          auto& disk_target =
              const_cast<DiskInfo&>(disk_manager.GetDisks()[disk_index]);
          bool success = disk_manager.PartitionAndFormatDisk(
              disk_target, scheme, parts_to_apply);
          Defer([progress_dialog, parent_window, disk_name,
                 on_partition_applied, success]() {
            CloseProgressDialog(progress_dialog);
            if (!success) {
              ShowMessageDialog(
                  "Partitioning Failed",
                  "Unable to partition " + disk_name +
                      ". Make sure the drive is unmounted and writable.",
                  parent_window);
            }
            if (on_partition_applied) on_partition_applied(success);
          });
        }).detach();
      },
      [](Button& button) { button.SetButtonStyle(ButtonStyle::PRIMARY); });
  *apply_button_node = apply_btn;

  *dialog_ptr = UiWindow::DialogWithTitleBar(
      "Partition Disk", UiWindow::Parent(parent_window),
      [dialog_ptr](UiWindow& window) {
        window.OnClose([dialog_ptr]() { CloseDialog(*dialog_ptr); });
      },
      [](Layout& layout) {
        layout.SetWidth(kPartitionDiskDialogWidth);
        layout.SetHeight(kPartitionDiskDialogHeight);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetWidthPercent(100.0f);
            layout.SetHeightPercent(100.0f);
          },
          Label::BasicLabel(
              "Partition Disk: " + disk.name,
              [](Label& label) { label.SetFont(GetBold12UiFont()); }),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetAlignItems(YGAlignCenter);
              },
              Label::BasicLabel(
                  "Partition Scheme:",
                  [](Label& label) { label.SetFont(GetBold12UiFont()); }),
              DropDownBox::BasicDropDownBox(
                  scheme_options, *selected_scheme_idx,
                  [selected_scheme_idx, planned_partitions,
                   update_dialog_content](int idx) {
                    if (*selected_scheme_idx == kGptSchemeIndex &&
                        idx == kMbrSchemeIndex &&
                        planned_partitions->size() > kMaxMbrPartitions)
                      planned_partitions->resize(kMaxMbrPartitions);

                    *selected_scheme_idx = idx;
                    (*update_dialog_content)();
                  },
                  [](Layout& layout) { layout.SetFlexGrow(1.0f); })),
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetWidthPercent(100.0f);
              },
              dynamic_content_container.get()),
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
              apply_btn)));

  active_dialogs.push_back(*dialog_ptr);
  (*update_dialog_content)();
}

}  // namespace dialogs
