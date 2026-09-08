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

#include "disk_utility_window.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

#include "dialogs/change_mount_path_dialog.h"
#include "dialogs/format_dialog.h"
#include "dialogs/partition_disk_dialog.h"
#include "include/core/SkColor.h"
#include "perception/file.h"
#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/group_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/resizable_container.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/segmented_bar.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/point.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

using ::perception::Defer;
using ::perception::FormatSize;
using ::perception::GetService;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::TerminateProcess;
using ::perception::disk::DiskInfo;
using ::perception::disk::DiskManager;
using ::perception::disk::FilesystemType;
using ::perception::disk::FilesystemTypeToString;
using ::perception::disk::FreeSpaceRange;
using ::perception::disk::PartitionInfo;
using ::perception::disk::PartitionScheme;
using ::perception::disk::PlannedPartition;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::GetUiFont;
using ::perception::ui::kMarginAroundWidgets;
using ::perception::ui::kSecondaryTextColor;
using ::perception::ui::kUiWindowPadding;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::GroupBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ResizableContainer;
using ::perception::ui::components::ResizableContainerItem;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::SegmentedBar;
using ::perception::ui::components::SegmentedBarSegment;
using ::perception::ui::components::Tooltip;
using ::perception::ui::components::TreeView;
using ::perception::ui::components::TreeViewItem;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;
using ::perception::window::MouseButton;

namespace {

// Default main window width in pixels.
constexpr float kWindowWidth = 800.0f;

// Default main window height in pixels.
constexpr float kWindowHeight = 540.0f;

// Left sidebar navigation width in pixels.
constexpr float kSidebarWidth = 240.0f;

// Palette for partition segments in storage bar.
constexpr SkColor kSegmentColors[] = {
    SkColorSetARGB(0xFF, 0x3B, 0x82, 0xF6),  // Blue 500
    SkColorSetARGB(0xFF, 0x10, 0xB9, 0x81),  // Emerald 500
    SkColorSetARGB(0xFF, 0xF5, 0x9E, 0x0B),  // Amber 500
    SkColorSetARGB(0xFF, 0x8B, 0x5C, 0xF6),  // Purple 500
    SkColorSetARGB(0xFF, 0xEC, 0x48, 0x99),  // Pink 500
    SkColorSetARGB(0xFF, 0x06, 0xB6, 0xD4),  // Cyan 500
};

// Segment color for used space inside a partition.
constexpr SkColor kUsedSpaceColor = SkColorSetARGB(0xFF, 0x3B, 0x82, 0xF6);

// Segment color for free space inside a partition.
constexpr SkColor kFreeSpaceColor = SkColorSetARGB(0xFF, 0x93, 0xC5, 0xFD);

// Segment color for unallocated space on disk.
constexpr SkColor kUnallocatedColor = SkColorSetARGB(0xFF, 0xD1, 0xD5, 0xDB);

// Creates a standard metric row with bold key label and secondary value label.
std::shared_ptr<Node> CreateMetricRow(const std::string& key,
                                      const std::string& value) {
  return Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
      },
      Label::BasicLabel(
          key,
          [](Layout& layout) {
            layout.SetWidth(150.0f);
            layout.SetFlexShrink(0.0f);
          },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleLeft);
            label.SetColor(kSecondaryTextColor);
            label.SetFont(GetBold12UiFont());
          }),
      Label::BasicLabel(
          value,
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
          },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleLeft);
          }));
}

// Creates a Format button for formatting a volume.
std::shared_ptr<Node> CreateFormatButton(std::function<void()> on_format,
                                         bool is_writable, bool is_mounted) {
  bool can_format = is_writable && !is_mounted;
  auto format_btn = Button::TextButton(
      "Format",
      [on_format, can_format]() {
        if (!can_format) return;
        on_format();
      },
      [can_format](Button& button) {
        button.SetButtonStyle(can_format ? ButtonStyle::SECONDARY
                                         : ButtonStyle::DISABLED);
      });
  if (!is_writable)
    Tooltip::Attach(format_btn, "Read-only drives cannot be formatted");
  else if (is_mounted)
    Tooltip::Attach(format_btn, "Volume must be unmounted before formatting");
  return format_btn;
}

// Creates an Explore button that launches the given mount point path.
std::shared_ptr<Node> CreateExploreButton(bool is_mounted,
                                          std::string_view mount_point) {
  std::string path;
  if (!mount_point.empty()) {
    if (mount_point.front() != '/') path += '/';
    path += mount_point;
    if (path.back() != '/') path += '/';
  }

  bool can_explore = is_mounted && !path.empty();
  auto explore_btn = Button::TextButton(
      "Explore",
      [can_explore, path]() {
        if (!can_explore) return;
        Defer([path]() {
          LoadApplicationRequest request;
          request.name = path;
          GetService<Loader>().LaunchApplication(request, nullptr);
        });
      },
      [can_explore](Button& button) {
        button.SetButtonStyle(can_explore ? ButtonStyle::SECONDARY
                                          : ButtonStyle::DISABLED);
      });
  if (!can_explore)
    Tooltip::Attach(explore_btn, "Volume must be mounted first");
  return explore_btn;
}

}  // namespace

DiskUtilityWindow::DiskUtilityWindow(DiskManager& disk_manager)
    : disk_manager_(disk_manager) {}

void DiskUtilityWindow::Initialize() {
  BuildUi();
  disk_manager_.OnDisksUpdated([this]() {
    RefreshSidebar();
    RefreshDetails();
  });
  RefreshSidebar();
  const auto& disks = disk_manager_.GetDisks();
  if (!disks.empty()) SelectDisk(0);
}

void DiskUtilityWindow::BuildUi() {
  sidebar_tree_view_ = TreeView::Create();

  auto left_sidebar = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
      },
      [](Layout& layout) {
        layout.SetWidth(kSidebarWidth);
        layout.SetHeightPercent(100.0f);
        layout.SetMinHeight(0.0f);
      },
      sidebar_tree_view_);

  auto details_content = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetPadding(YGEdgeAll, 16.0f);
        layout.SetGap(16.0f);
      },
      &details_container_);

  auto details_scroll = ScrollContainer::BidirectionalScrollContainer(
      details_content, [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetMinWidth(0.0f);
      });

  auto right_pane = Container::VerticalContainer(
      [](ResizableContainerItem& item) {
        item.SetBehavior(ResizableContainerItem::Behavior::Flex);
      },
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetAlignItems(YGAlignStretch);
      },
      details_scroll);

  auto split_container = ResizableContainer::HorizontalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetWidthPercent(100.0f);
        layout.SetHeightPercent(100.0f);
      },
      left_sidebar, right_pane);

  window_node_ = UiWindow::ResizableWindowWithTitleBar(
      "Disk Utility",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      [](Layout& layout) {
        layout.SetWidth(kWindowWidth);
        layout.SetHeight(kWindowHeight);
      },
      split_container);
}

void DiskUtilityWindow::SelectDisk(int disk_index) {
  selected_disk_index_ = disk_index;
  selected_partition_number_ = 0;
  selected_free_space_index_ = -1;
  RefreshDetails();
}

void DiskUtilityWindow::SelectPartition(int disk_index, int partition_number) {
  selected_disk_index_ = disk_index;
  selected_partition_number_ = partition_number;
  selected_free_space_index_ = -1;
  RefreshDetails();
}

void DiskUtilityWindow::SelectFreeSpace(int disk_index, int free_space_index) {
  selected_disk_index_ = disk_index;
  selected_partition_number_ = -2;
  selected_free_space_index_ = free_space_index;
  RefreshDetails();
}

void DiskUtilityWindow::RefreshSidebar() {
  if (!sidebar_tree_view_) return;

  auto tree = sidebar_tree_view_->Get<TreeView>();
  if (!tree) return;

  auto content_container = tree->GetContentContainer();
  if (!content_container) return;
  content_container->RemoveChildren();

  const auto& disks = disk_manager_.GetDisks();
  std::shared_ptr<TreeViewItem> item_to_select;

  for (size_t d = 0; d < disks.size(); d++) {
    const auto& disk = disks[d];
    int disk_idx = static_cast<int>(d);
    std::string disk_label =
        disk.name + " (" + FormatSize(disk.size_in_bytes) + ")";

    std::vector<std::shared_ptr<Node>> child_item_nodes;

    if (disk.partitions.empty()) {
      if (disk.is_mounted) {
        std::string vol_label = "/" + disk.mount_point;
        auto vol_item_node = TreeViewItem::Item(vol_label);
        auto vol_item = vol_item_node->Get<TreeViewItem>();
        if (vol_item) {
          vol_item->OnSelect([this, disk_idx]() { SelectDisk(disk_idx); });
          if (selected_disk_index_ == disk_idx &&
              selected_partition_number_ == 0) {
            item_to_select = vol_item;
          }
        }
        child_item_nodes.push_back(vol_item_node);
      }
    } else {
      for (const auto& part : disk.partitions) {
        std::string part_label = part.name;
        if (part.is_mounted) part_label += " (/" + part.mount_point + ")";

        int part_num = part.partition_number;
        auto part_item_node = TreeViewItem::Item(part_label);
        auto part_item = part_item_node->Get<TreeViewItem>();
        if (part_item) {
          part_item->OnSelect([this, disk_idx, part_num]() {
            SelectPartition(disk_idx, part_num);
          });
          if (selected_disk_index_ == disk_idx &&
              selected_partition_number_ == part_num) {
            item_to_select = part_item;
          }
        }
        child_item_nodes.push_back(part_item_node);
      }
    }

    for (size_t f = 0; f < disk.free_space_ranges.size(); f++) {
      std::string free_label =
          "Free Space (" + FormatSize(disk.free_space_ranges[f].size_in_bytes) +
          ")";

      int free_idx = static_cast<int>(f);
      auto free_item_node = TreeViewItem::Item(free_label);
      auto free_item = free_item_node->Get<TreeViewItem>();
      if (free_item) {
        free_item->OnSelect([this, disk_idx, free_idx]() {
          SelectFreeSpace(disk_idx, free_idx);
        });
        if (selected_disk_index_ == disk_idx &&
            selected_partition_number_ == -2 &&
            selected_free_space_index_ == free_idx) {
          item_to_select = free_item;
        }
      }
      child_item_nodes.push_back(free_item_node);
    }

    auto disk_item_node = TreeViewItem::Item(disk_label, child_item_nodes);
    auto disk_item = disk_item_node->Get<TreeViewItem>();
    if (disk_item) {
      if (!child_item_nodes.empty()) disk_item->SetExpanded(true);
      disk_item->OnSelect([this, disk_idx]() { SelectDisk(disk_idx); });
      if (selected_disk_index_ == disk_idx && selected_partition_number_ == 0 &&
          !item_to_select) {
        item_to_select = disk_item;
      }
    }

    content_container->AddChild(disk_item_node);
  }

  if (item_to_select) {
    tree->SetSelectedItem(item_to_select, false);
  }
}

std::shared_ptr<Node> DiskUtilityWindow::BuildDetailsHeader() {
  const auto& disk = disk_manager_.GetDisks()[selected_disk_index_];
  std::string title;
  std::string subtitle;

  if (selected_partition_number_ == 0) {
    title = disk.name;
    std::string scheme_str = "No Partition Table";
    if (disk.scheme == PartitionScheme::GPT)
      scheme_str = "GUID Partition Table (GPT)";
    else if (disk.scheme == PartitionScheme::MBR)
      scheme_str = "Master Boot Record (MBR)";
    subtitle = scheme_str + " • " + FormatSize(disk.size_in_bytes);
    if (disk.is_mounted) subtitle += " • Mounted at /" + disk.mount_point;
  } else if (selected_partition_number_ > 0) {
    auto part_itr =
        std::find_if(disk.partitions.begin(), disk.partitions.end(),
                     [this](const PartitionInfo& p) {
                       return p.partition_number == selected_partition_number_;
                     });
    if (part_itr != disk.partitions.end()) {
      title = part_itr->name;
      subtitle = part_itr->filesystem_name + " • " +
                 FormatSize(part_itr->size_in_bytes);
      if (part_itr->is_mounted)
        subtitle += " • Mounted at /" + part_itr->mount_point;
    }
  } else if (selected_partition_number_ == -2 &&
             selected_free_space_index_ >= 0 &&
             selected_free_space_index_ <
                 static_cast<int>(disk.free_space_ranges.size())) {
    title = "Unallocated Free Space";
    subtitle =
        "Free Space • " +
        FormatSize(
            disk.free_space_ranges[selected_free_space_index_].size_in_bytes);
  }

  return Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetGap(2.0f);
      },
      Label::BasicLabel(
          title,
          [](Label& label) { label.SetFont(GetUiFont("", 16.0f, true)); }),
      Label::BasicLabel(subtitle, [](Label& label) {
        label.SetColor(kSecondaryTextColor);
      }));
}

std::shared_ptr<Node> DiskUtilityWindow::BuildStorageBarAndLegend() {
  const auto& disk = disk_manager_.GetDisks()[selected_disk_index_];
  std::vector<SegmentedBarSegment> bar_segments;

  if (selected_partition_number_ == 0) {
    if (disk.partitions.empty()) {
      if (disk.raw_filesystem != FilesystemType::UNKNOWN &&
          disk.raw_filesystem != FilesystemType::RAW) {
        float used_ratio =
            disk.size_in_bytes > 0
                ? static_cast<float>(disk.raw_used_bytes) / disk.size_in_bytes
                : 0.0f;
        bar_segments.push_back({"Used", used_ratio, kUsedSpaceColor});
        bar_segments.push_back({"Free", 1.0f - used_ratio, kFreeSpaceColor});
      } else {
        bar_segments.push_back({"Storage", 1.0f, kUsedSpaceColor});
      }
    } else {
      size_t color_idx = 0;
      for (const auto& part : disk.partitions) {
        float ratio =
            disk.size_in_bytes > 0
                ? static_cast<float>(part.size_in_bytes) / disk.size_in_bytes
                : 0.0f;
        bar_segments.push_back(
            {part.name, ratio, kSegmentColors[color_idx % 6]});
        color_idx++;
      }
      for (const auto& free_r : disk.free_space_ranges) {
        float ratio =
            disk.size_in_bytes > 0
                ? static_cast<float>(free_r.size_in_bytes) / disk.size_in_bytes
                : 0.0f;
        bar_segments.push_back({"Free Space", ratio, kUnallocatedColor});
      }
    }
  } else if (selected_partition_number_ > 0) {
    auto part_itr =
        std::find_if(disk.partitions.begin(), disk.partitions.end(),
                     [this](const PartitionInfo& p) {
                       return p.partition_number == selected_partition_number_;
                     });
    if (part_itr != disk.partitions.end()) {
      if (part_itr->filesystem_type != FilesystemType::UNKNOWN &&
          part_itr->filesystem_type != FilesystemType::RAW) {
        float used_ratio = part_itr->size_in_bytes > 0
                               ? static_cast<float>(part_itr->used_bytes) /
                                     part_itr->size_in_bytes
                               : 0.0f;
        bar_segments.push_back({"Used", used_ratio, kUsedSpaceColor});
        bar_segments.push_back({"Free", 1.0f - used_ratio, kFreeSpaceColor});
      } else {
        bar_segments.push_back({part_itr->name, 1.0f, kUsedSpaceColor});
      }
    }
  } else {
    bar_segments.push_back({"Free Space", 1.0f, kUnallocatedColor});
  }

  auto bar_node = SegmentedBar::BasicSegmentedBar(
      storage_bar_,
      [&bar_segments](SegmentedBar& bar) { bar.SetSegments(bar_segments); });

  auto legend_container = Container::HorizontalContainer([](Layout& layout) {
    layout.SetWidthPercent(100.0f);
    layout.SetAlignItems(YGAlignCenter);
    layout.SetFlexWrap(YGWrapWrap);
  });

  for (const auto& seg : bar_segments) {
    if (seg.ratio <= 0.0f) continue;
    int pct = static_cast<int>(seg.ratio * 100.0f + 0.5f);
    std::string badge_text = seg.label + " (" + std::to_string(pct) + "%)";

    auto item = Container::HorizontalContainer(
        [](Layout& layout) {
          layout.SetAlignItems(YGAlignCenter);
          layout.SetGap(kMarginAroundWidgets);
        },
        Node::Empty(
            [](Layout& layout) {
              layout.SetWidth(10.0f);
              layout.SetHeight(10.0f);
              layout.SetFlexShrink(0.0f);
            },
            [color = seg.color](Block& block) {
              block.SetBorderRadius(5.0f);
              block.SetFillColor(color);
            }),
        Label::BasicLabel(badge_text, [](Label& label) {
          label.SetColor(kSecondaryTextColor);
        }));
    legend_container->AddChild(item);
  }

  return Container::VerticalContainer(
      [](Layout& layout) { layout.SetWidthPercent(100.0f); }, bar_node,
      legend_container);
}

std::shared_ptr<Node> DiskUtilityWindow::BuildMetricsBox() {
  const auto& disk = disk_manager_.GetDisks()[selected_disk_index_];
  std::string group_title = "Device Information";
  if (selected_partition_number_ > 0) {
    group_title = "Partition Information";
  } else if (selected_partition_number_ == -2) {
    group_title = "Free Space Information";
  }

  auto metrics_box = GroupBox::VerticalGroupBox(
      group_title, [](Layout& layout) { layout.SetWidthPercent(100.0f); });

  if (selected_partition_number_ == 0) {
    std::string scheme_name = "None (Unpartitioned)";
    if (disk.scheme == PartitionScheme::GPT)
      scheme_name = "GPT (GUID Partition Table)";
    else if (disk.scheme == PartitionScheme::MBR)
      scheme_name = "MBR (Master Boot Record)";

    metrics_box->AddChild(CreateMetricRow("Partition Scheme", scheme_name));
    metrics_box->AddChild(
        CreateMetricRow("Capacity", FormatSize(disk.size_in_bytes)));

    if (disk.scheme == PartitionScheme::NONE &&
        disk.raw_filesystem != FilesystemType::UNKNOWN) {
      metrics_box->AddChild(CreateMetricRow(
          "Filesystem",
          std::string(FilesystemTypeToString(disk.raw_filesystem))));
      if (disk.raw_filesystem != FilesystemType::RAW) {
        metrics_box->AddChild(
            CreateMetricRow("Used Space", FormatSize(disk.raw_used_bytes)));
        metrics_box->AddChild(
            CreateMetricRow("Free Space", FormatSize(disk.raw_free_bytes)));
      }
    }

    bool is_partitioned =
        (disk.scheme != PartitionScheme::NONE || !disk.partitions.empty());
    if (is_partitioned) {
      size_t mounted_count = 0;
      for (const auto& part : disk.partitions) {
        if (part.is_mounted) mounted_count++;
      }
      if (mounted_count == 0) {
        metrics_box->AddChild(CreateMetricRow("Mount Status", "Not Mounted"));
      } else if (mounted_count == disk.partitions.size()) {
        metrics_box->AddChild(
            CreateMetricRow("Mount Status", "All partitions mounted"));
      } else {
        metrics_box->AddChild(CreateMetricRow(
            "Mount Status", std::to_string(mounted_count) + " of " +
                                std::to_string(disk.partitions.size()) +
                                " partitions mounted"));
      }
      if (disk.is_boot_drive)
        metrics_box->AddChild(
            CreateMetricRow("Boot Disk", "Yes (System Boot Volume)"));
    } else {
      if (disk.is_mounted) {
        metrics_box->AddChild(
            CreateMetricRow("Mount Status", "Mounted at /" + disk.mount_point));
        if (disk.is_boot_drive)
          metrics_box->AddChild(
              CreateMetricRow("Boot Disk", "Yes (System Boot Volume)"));
      } else {
        metrics_box->AddChild(CreateMetricRow("Mount Status", "Not Mounted"));
      }
    }
    metrics_box->AddChild(CreateMetricRow(
        "Sector Size", std::to_string(disk.sector_size) + " bytes"));
    metrics_box->AddChild(
        CreateMetricRow("Total Sectors", std::to_string(disk.total_sectors)));
    metrics_box->AddChild(CreateMetricRow(
        "Writable", disk.is_writable ? "Yes" : "No (Read-Only)"));
  } else if (selected_partition_number_ > 0) {
    auto part_itr =
        std::find_if(disk.partitions.begin(), disk.partitions.end(),
                     [this](const PartitionInfo& p) {
                       return p.partition_number == selected_partition_number_;
                     });
    if (part_itr != disk.partitions.end()) {
      metrics_box->AddChild(
          CreateMetricRow("Filesystem", part_itr->filesystem_name));
      metrics_box->AddChild(
          CreateMetricRow("Partition Type", part_itr->type_name));
      metrics_box->AddChild(
          CreateMetricRow("Capacity", FormatSize(part_itr->size_in_bytes)));
      if (part_itr->filesystem_type != FilesystemType::UNKNOWN &&
          part_itr->filesystem_type != FilesystemType::RAW) {
        metrics_box->AddChild(
            CreateMetricRow("Used Space", FormatSize(part_itr->used_bytes)));
        metrics_box->AddChild(
            CreateMetricRow("Free Space", FormatSize(part_itr->free_bytes)));
      }
      if (part_itr->is_mounted) {
        metrics_box->AddChild(CreateMetricRow(
            "Mount Status", "Mounted at /" + part_itr->mount_point));
        if (part_itr->is_boot_drive)
          metrics_box->AddChild(
              CreateMetricRow("Boot Disk", "Yes (System Boot Volume)"));
      } else {
        metrics_box->AddChild(CreateMetricRow("Mount Status", "Not Mounted"));
      }
      std::string lba_range = "Sectors " + std::to_string(part_itr->start_lba) +
                              " - " + std::to_string(part_itr->end_lba) + " (" +
                              std::to_string(part_itr->sector_count) +
                              " sectors)";
      metrics_box->AddChild(CreateMetricRow("LBA Range", lba_range));
    }
  } else if (selected_partition_number_ == -2 &&
             selected_free_space_index_ >= 0 &&
             selected_free_space_index_ <
                 static_cast<int>(disk.free_space_ranges.size())) {
    const auto& range = disk.free_space_ranges[selected_free_space_index_];
    metrics_box->AddChild(
        CreateMetricRow("Available Space", FormatSize(range.size_in_bytes)));
    std::string lba_range = "Sectors " + std::to_string(range.start_lba) +
                            " - " + std::to_string(range.end_lba) + " (" +
                            std::to_string(range.sector_count) + " sectors)";
    metrics_box->AddChild(CreateMetricRow("LBA Range", lba_range));
  }

  return metrics_box;
}

std::shared_ptr<Node> DiskUtilityWindow::BuildPartitionsBox() {
  const auto& disk = disk_manager_.GetDisks()[selected_disk_index_];
  int disk_idx = selected_disk_index_;

  auto partitions_box = GroupBox::VerticalGroupBox(
      "Partitions on this Device",
      [](Layout& layout) { layout.SetWidthPercent(100.0f); });

  if (disk.partitions.empty()) {
    std::string empty_msg =
        (disk.scheme == PartitionScheme::NONE)
            ? "Single unpartitioned volume (no partition table)."
            : "No partitions defined on this disk. Click 'Partition Disk' "
              "to create partitions.";
    partitions_box->AddChild(Label::BasicLabel(empty_msg, [](Label& label) {
      label.SetColor(kSecondaryTextColor);
    }));
  } else {
    for (const auto& part : disk.partitions) {
      int part_num = part.partition_number;
      std::string part_title =
          "#" + std::to_string(part.partition_number) + ": " + part.name;
      std::string part_sub =
          part.filesystem_name + " • " + FormatSize(part.size_in_bytes);
      if (part.is_mounted)
        part_sub += " • Mounted at /" + part.mount_point;
      else
        part_sub += " • Not Mounted";

      auto part_card = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
          },
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetGap(2.0f);
              },
              Label::BasicLabel(
                  part_title,
                  [](Label& label) { label.SetFont(GetBold12UiFont()); }),
              Label::BasicLabel(part_sub,
                                [](Label& label) {
                                  label.SetColor(kSecondaryTextColor);
                                })),
          Button::TextButton(
              "View",
              [this, disk_idx, part_num]() {
                SelectPartition(disk_idx, part_num);
                RefreshSidebar();
              },
              [](Button& button) {
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              }));

      partitions_box->AddChild(part_card);
    }

    for (size_t f = 0; f < disk.free_space_ranges.size(); f++) {
      int free_idx = static_cast<int>(f);
      std::string free_title = "Unallocated Free Space";
      std::string free_sub =
          FormatSize(disk.free_space_ranges[f].size_in_bytes) + " available";

      auto free_card = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
          },
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetGap(2.0f);
              },
              Label::BasicLabel(
                  free_title,
                  [](Label& label) { label.SetFont(GetBold12UiFont()); }),
              Label::BasicLabel(free_sub,
                                [](Label& label) {
                                  label.SetColor(kSecondaryTextColor);
                                })),
          Button::TextButton(
              "View",
              [this, disk_idx, free_idx]() {
                SelectFreeSpace(disk_idx, free_idx);
                RefreshSidebar();
              },
              [](Button& button) {
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              }));

      partitions_box->AddChild(free_card);
    }
  }

  return partitions_box;
}

std::shared_ptr<Node> DiskUtilityWindow::BuildActionsToolbar() {
  const auto& disk = disk_manager_.GetDisks()[selected_disk_index_];
  int disk_idx = selected_disk_index_;

  auto actions_toolbar = Container::HorizontalContainer([](Layout& layout) {
    layout.SetWidthPercent(100.0f);
    layout.SetAlignItems(YGAlignCenter);
    layout.SetFlexWrap(YGWrapWrap);
  });

  if (selected_partition_number_ == 0) {
    bool is_partitioned =
        (disk.scheme != PartitionScheme::NONE || !disk.partitions.empty());

    if (is_partitioned) {
      size_t mounted_count = 0;
      size_t unmounted_count = 0;
      bool all_mounted_are_boot = true;

      for (const auto& part : disk.partitions) {
        if (part.is_mounted) {
          mounted_count++;
          if (!part.is_boot_drive) all_mounted_are_boot = false;
        } else {
          unmounted_count++;
        }
      }

      bool has_partitions = !disk.partitions.empty();

      if (unmounted_count > 0 || !has_partitions) {
        bool can_mount = has_partitions;
        auto mount_btn = Button::TextButton(
            "Mount",
            [this, disk_idx, can_mount]() {
              if (!can_mount) return;
              Defer([this, disk_idx]() {
                const auto& disks = disk_manager_.GetDisks();
                if (disk_idx < static_cast<int>(disks.size())) {
                  auto& d = const_cast<DiskInfo&>(disks[disk_idx]);
                  disk_manager_.MountAllPartitions(d);
                }
              });
            },
            [can_mount](Button& button) {
              if (can_mount)
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              else
                button.SetButtonStyle(ButtonStyle::DISABLED);
            });
        if (!has_partitions)
          Tooltip::Attach(mount_btn, "Drive contains no partitions to mount");
        actions_toolbar->AddChild(mount_btn);
      }

      if (mounted_count > 0) {
        bool can_unmount = !all_mounted_are_boot;
        auto unmount_btn = Button::TextButton(
            "Unmount",
            [this, disk_idx, can_unmount]() {
              if (!can_unmount) return;
              Defer([this, disk_idx]() {
                const auto& disks = disk_manager_.GetDisks();
                if (disk_idx < static_cast<int>(disks.size())) {
                  auto& d = const_cast<DiskInfo&>(disks[disk_idx]);
                  disk_manager_.UnmountAllPartitions(d);
                }
              });
            },
            [can_unmount](Button& button) {
              if (can_unmount)
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              else
                button.SetButtonStyle(ButtonStyle::DISABLED);
            });
        if (all_mounted_are_boot)
          Tooltip::Attach(unmount_btn, "Boot disk cannot be unmounted");
        actions_toolbar->AddChild(unmount_btn);
      }
    } else {
      std::shared_ptr<Node> mount_unmount_btn;
      if (disk.is_mounted) {
        bool is_boot = disk.is_boot_drive;
        std::string mount_pt = disk.mount_point;
        mount_unmount_btn = Button::TextButton(
            "Unmount",
            [this, is_boot, mount_pt]() {
              if (is_boot) return;
              Defer([this, mount_pt]() { disk_manager_.Unmount(mount_pt); });
            },
            [is_boot](Button& button) {
              if (is_boot)
                button.SetButtonStyle(ButtonStyle::DISABLED);
              else
                button.SetButtonStyle(ButtonStyle::SECONDARY);
            });
        if (is_boot)
          Tooltip::Attach(mount_unmount_btn, "Boot disk cannot be unmounted");
      } else {
        bool can_mount = (disk.raw_filesystem != FilesystemType::UNKNOWN &&
                          disk.raw_filesystem != FilesystemType::RAW);
        mount_unmount_btn = Button::TextButton(
            "Mount",
            [this, disk_idx, can_mount]() {
              if (!can_mount) return;
              Defer([this, disk_idx]() {
                const auto& disks = disk_manager_.GetDisks();
                if (disk_idx < static_cast<int>(disks.size())) {
                  auto& d = const_cast<DiskInfo&>(disks[disk_idx]);
                  disk_manager_.MountDisk(d);
                }
              });
            },
            [can_mount](Button& button) {
              if (can_mount)
                button.SetButtonStyle(ButtonStyle::SECONDARY);
              else
                button.SetButtonStyle(ButtonStyle::DISABLED);
            });
        if (!can_mount)
          Tooltip::Attach(mount_unmount_btn,
                          "Drive contains no mountable filesystem");
      }
      actions_toolbar->AddChild(mount_unmount_btn);

      bool is_mounted = disk.is_mounted;
      std::string mount_pt = disk.mount_point;
      auto change_mount_btn = Button::TextButton(
          "Change Mount Path",
          [this, is_mounted, mount_pt]() {
            if (!is_mounted) return;
            Defer([this, mount_pt]() { ShowChangeMountPathDialog(mount_pt); });
          },
          [is_mounted](Button& button) {
            if (is_mounted)
              button.SetButtonStyle(ButtonStyle::SECONDARY);
            else
              button.SetButtonStyle(ButtonStyle::DISABLED);
          });
      if (!is_mounted)
        Tooltip::Attach(change_mount_btn,
                        "Drive must be mounted to change mount path");
      actions_toolbar->AddChild(change_mount_btn);
    }

    bool is_writable = disk.is_writable;
    bool any_mounted = disk_manager_.IsDiskOrAnyPartitionMounted(disk);
    bool can_partition = is_writable && !any_mounted;
    auto part_btn = Button::TextButton(
        "Partition Disk",
        [this, can_partition]() {
          if (!can_partition) return;
          Defer([this]() { ShowPartitionDiskDialog(); });
        },
        [can_partition](Button& button) {
          if (can_partition)
            button.SetButtonStyle(ButtonStyle::PRIMARY);
          else
            button.SetButtonStyle(ButtonStyle::DISABLED);
        });
    if (!is_writable)
      Tooltip::Attach(part_btn, "Read-only drives cannot be partitioned");
    else if (any_mounted)
      Tooltip::Attach(part_btn, "Drive must be unmounted before partitioning");
    actions_toolbar->AddChild(part_btn);

    if (disk.scheme == PartitionScheme::NONE) {
      actions_toolbar->AddChild(CreateFormatButton(
          [this]() { Defer([this]() { ShowFormatDialog(); }); }, is_writable,
          disk.is_mounted));
      actions_toolbar->AddChild(
          CreateExploreButton(disk.is_mounted, disk.mount_point));
    }
  } else if (selected_partition_number_ > 0) {
    auto part_itr =
        std::find_if(disk.partitions.begin(), disk.partitions.end(),
                     [this](const PartitionInfo& p) {
                       return p.partition_number == selected_partition_number_;
                     });
    if (part_itr != disk.partitions.end()) {
      int part_num = part_itr->partition_number;
      if (part_itr->is_mounted) {
        bool is_boot = part_itr->is_boot_drive;
        std::string mount_pt = part_itr->mount_point;
        auto unmount_btn = Button::TextButton(
            "Unmount",
            [this, is_boot, mount_pt]() {
              if (is_boot) return;
              Defer([this, mount_pt]() { disk_manager_.Unmount(mount_pt); });
            },
            [is_boot](Button& button) {
              if (is_boot)
                button.SetButtonStyle(ButtonStyle::DISABLED);
              else
                button.SetButtonStyle(ButtonStyle::SECONDARY);
            });
        if (is_boot)
          Tooltip::Attach(unmount_btn, "Boot disk cannot be unmounted");
        actions_toolbar->AddChild(unmount_btn);

        actions_toolbar->AddChild(Button::TextButton(
            "Change Mount Path",
            [this, mount_pt]() {
              Defer(
                  [this, mount_pt]() { ShowChangeMountPathDialog(mount_pt); });
            },
            [](Button& button) {
              button.SetButtonStyle(ButtonStyle::SECONDARY);
            }));
      } else if (part_itr->filesystem_type != FilesystemType::UNKNOWN &&
                 part_itr->filesystem_type != FilesystemType::RAW) {
        actions_toolbar->AddChild(Button::TextButton(
            "Mount",
            [this, disk_idx, part_num]() {
              Defer([this, disk_idx, part_num]() {
                const auto& disks = disk_manager_.GetDisks();
                if (disk_idx < static_cast<int>(disks.size())) {
                  auto& d = const_cast<DiskInfo&>(disks[disk_idx]);
                  disk_manager_.MountPartition(d, part_num);
                }
              });
            },
            [](Button& button) {
              button.SetButtonStyle(ButtonStyle::SECONDARY);
            }));
      }

      actions_toolbar->AddChild(CreateFormatButton(
          [this]() { Defer([this]() { ShowFormatDialog(); }); },
          disk.is_writable, part_itr->is_mounted));
      actions_toolbar->AddChild(
          CreateExploreButton(part_itr->is_mounted, part_itr->mount_point));
    }
  } else if (selected_partition_number_ == -2) {
    bool is_writable = disk.is_writable;
    bool any_mounted = disk_manager_.IsDiskOrAnyPartitionMounted(disk);
    bool can_partition = is_writable && !any_mounted;
    auto part_btn = Button::TextButton(
        "Partition Disk",
        [this, can_partition]() {
          if (!can_partition) return;
          Defer([this]() { ShowPartitionDiskDialog(); });
        },
        [can_partition](Button& button) {
          if (can_partition)
            button.SetButtonStyle(ButtonStyle::PRIMARY);
          else
            button.SetButtonStyle(ButtonStyle::DISABLED);
        });
    if (!is_writable)
      Tooltip::Attach(part_btn, "Read-only drives cannot be partitioned");
    else if (any_mounted)
      Tooltip::Attach(part_btn, "Drive must be unmounted before partitioning");
    actions_toolbar->AddChild(part_btn);
  }

  return actions_toolbar;
}

void DiskUtilityWindow::RefreshDetails() {
  if (!details_container_) return;
  details_container_->RemoveChildren();

  const auto& disks = disk_manager_.GetDisks();
  if (selected_disk_index_ < 0 ||
      selected_disk_index_ >= static_cast<int>(disks.size())) {
    details_container_->AddChild(Label::BasicLabel(
        "Select a disk or partition to view details", [](Label& label) {
          label.SetColor(kSecondaryTextColor);
        }));
    details_container_->Invalidate();
    return;
  }

  details_container_->AddChild(BuildDetailsHeader());
  details_container_->AddChild(BuildStorageBarAndLegend());
  details_container_->AddChild(BuildMetricsBox());
  if (selected_partition_number_ == 0)
    details_container_->AddChild(BuildPartitionsBox());
  details_container_->AddChild(BuildActionsToolbar());
  details_container_->Invalidate();
}

void DiskUtilityWindow::ShowPartitionDiskDialog() {
  if (selected_disk_index_ < 0) return;
  int disk_idx = selected_disk_index_;
  dialogs::ShowPartitionDiskDialog(disk_manager_, disk_idx, window_node_,
                                   [this, disk_idx](bool success) {
                                     RefreshSidebar();
                                     SelectDisk(disk_idx);
                                   });
}

void DiskUtilityWindow::ShowFormatDialog() {
  if (selected_disk_index_ < 0) return;
  int disk_idx = selected_disk_index_;
  int part_num = selected_partition_number_;
  dialogs::ShowFormatDialog(
      disk_manager_, disk_idx, part_num, window_node_,
      [this, disk_idx, part_num](bool success) {
        RefreshSidebar();
        if (disk_idx < static_cast<int>(disk_manager_.GetDisks().size())) {
          if (part_num > 0)
            SelectPartition(disk_idx, part_num);
          else
            SelectDisk(disk_idx);
        }
      });
}

void DiskUtilityWindow::ShowChangeMountPathDialog(
    std::string_view current_mount_point) {
  dialogs::ShowChangeMountPathDialog(disk_manager_, current_mount_point,
                                     window_node_,
                                     [this]() { RefreshDetails(); });
}
