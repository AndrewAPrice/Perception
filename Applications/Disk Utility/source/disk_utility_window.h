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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/disk/disk_manager.h"
#include "perception/ui/components/segmented_bar.h"
#include "perception/ui/node.h"

// Main window interface for Disk Utility application.
class DiskUtilityWindow {
 public:
  explicit DiskUtilityWindow(perception::disk::DiskManager& disk_manager);

  // Builds and initializes the application window.
  void Initialize();

  // Selects a whole disk.
  void SelectDisk(int disk_index);

  // Selects a partition within a disk.
  void SelectPartition(int disk_index, int partition_number);

  // Selects an unallocated free space span.
  void SelectFreeSpace(int disk_index, int free_space_index);

 private:
  // Reference to storage disk manager.
  perception::disk::DiskManager& disk_manager_;

  // Top-level UI window node.
  std::shared_ptr<perception::ui::Node> window_node_;

  // Tree view holding the device and partition hierarchy in left sidebar.
  std::shared_ptr<perception::ui::Node> sidebar_tree_view_;

  // Container holding the details view on right pane.
  std::shared_ptr<perception::ui::Node> details_container_;

  // Segmented capacity bar widget controller.
  std::shared_ptr<perception::ui::components::SegmentedBar> storage_bar_;

  // Selected disk index (-1 if none).
  int selected_disk_index_ = -1;

  // Selected partition number (-1 for none, 0 for entire disk, >0 for
  // partition).
  int selected_partition_number_ = -1;

  // Selected free space index (-1 for none).
  int selected_free_space_index_ = -1;

  // Builds the entire UI layout.
  void BuildUi();

  // Re-populates the left sidebar tree with disks and partitions.
  void RefreshSidebar();

  // Re-populates the right details pane for current selection.
  void RefreshDetails();

  // Builds the header section in details view.
  std::shared_ptr<perception::ui::Node> BuildDetailsHeader();

  // Builds the storage bar and its color-coded legend in details view.
  std::shared_ptr<perception::ui::Node> BuildStorageBarAndLegend();

  // Builds the device/partition/free space metrics group box.
  std::shared_ptr<perception::ui::Node> BuildMetricsBox();

  // Builds the partitions list card when a whole disk is selected.
  std::shared_ptr<perception::ui::Node> BuildPartitionsBox();

  // Builds the action buttons toolbar at the bottom of details view.
  std::shared_ptr<perception::ui::Node> BuildActionsToolbar();

  // Shows dialog to partition and format disk (GPT, MBR, or
  // Unpartitioned/Superfloppy).
  void ShowPartitionDiskDialog();

  // Shows dialog to format and erase.
  void ShowFormatDialog();

  // Shows dialog to change the mount path of a mounted volume.
  void ShowChangeMountPathDialog(std::string_view current_mount_point);
};
