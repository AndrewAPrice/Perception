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
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "file_list_view.h"
#include "file_operations.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"

// Top-level application window for File Manager, owning navigation history,
// clipboard operations, modal prompts, context menus, and the file list view.
class FileManagerWindow {
 public:
  // Constructs the File Manager window configured to open `initial_directory`.
  explicit FileManagerWindow(std::string_view initial_directory = "/");

  // Builds the window UI, loads the initial directory, and focuses the list.
  void Initialize();

 private:
  // Builds the top-level window, navigation toolbar, file list view, and status
  // bar.
  void BuildUi();

  // Navigates to `path`, optionally recording history and selecting the
  // specified paths once loaded.
  void NavigateTo(const std::string& path, bool record_history = true,
                  const std::set<std::string>& paths_to_select = {});

  // Navigates backward in directory history.
  void GoBack();

  // Navigates forward in directory history.
  void GoForward();

  // Navigates to the parent directory of the current path.
  void GoUp();

  // Updates the enabled state of the Back, Forward, and Up toolbar buttons.
  void UpdateNavigationButtons();

  // Updates the status bar label with the total item count and selection info.
  void UpdateStatusLabel();

  // Opens a directory in File Manager or launches an application/file via the
  // system Loader service.
  void OpenItem(const std::string& entry_path, bool is_dir);

  // Launches an application or file path via the Loader service while showing
  // the loading overlay.
  void LaunchFileOrApplication(const std::string& entry_path);

  // Shows or hides the loading overlay based on whether any launches are in
  // progress.
  void UpdateLoadingOverlay();

  // Opens the currently selected item or items.
  void OpenSelectedItems();

  // Copies the currently selected paths to the internal file clipboard.
  void CopySelectedItems();

  // Cuts the currently selected paths to the internal file clipboard.
  void CutSelectedItems();

  // Pastes items from the internal file clipboard into the current directory.
  void PasteClipboardItems();

  // Duplicates all currently selected items inside the current directory.
  void DuplicateSelectedItems();

  // Copies the selected item paths (or `fallback_path` if none) to the system
  // text clipboard.
  void CopySelectedPathsToClipboard(const std::string& fallback_path);

  // Prompts the user to create a new folder in the current directory.
  void PromptCreateNewFolder();

  // Prompts the user to create a new empty file in the current directory.
  void PromptCreateNewFile();

  // Prompts the user to rename the single selected item.
  void PromptRenameSelectedItem();

  // Prompts the user to confirm and delete the selected items.
  void PromptDeleteSelectedItems();

  // Displays the context menu for a specific file or directory row.
  void ShowContextMenuForItem(perception::ui::Node& context_node,
                              const perception::ui::Point& point,
                              const std::string& entry_path, bool is_dir);

  // Displays the context menu for the empty directory background.
  void ShowContextMenuForBackground(perception::ui::Node& context_node,
                                    const perception::ui::Point& point);

  // Initial directory path to open on startup.
  std::string initial_directory_;

  // Active directory path currently displayed.
  std::string current_path_ = "/";

  // Active column used to sort directory entries.
  SortColumn sort_column_ = SortColumn::NAME;

  // Active direction used to sort directory entries.
  SortDirection sort_direction_ = SortDirection::ASCENDING;

  // Whether dot-prefixed hidden files and folders are shown.
  bool show_hidden_files_ = false;

  // Stack of previously visited directory paths for Back navigation.
  std::vector<std::string> back_history_;

  // Stack of directory paths for Forward navigation.
  std::vector<std::string> forward_history_;

  // Paths stored in the internal file clipboard for Copy/Cut/Paste.
  std::vector<std::string> file_clipboard_paths_;

  // Whether the internal file clipboard represents a Cut (move) operation.
  bool file_clipboard_is_cut_ = false;

  // Number of application launches currently in flight.
  size_t pending_launches_ = 0;

  // File list view component managing headers, rows, and selection.
  std::unique_ptr<FileListView> file_list_view_;

  // Top-level window UI node.
  std::shared_ptr<perception::ui::Node> main_window_;

  // Overlay node displayed while an application is launching.
  std::shared_ptr<perception::ui::Node> loading_overlay_;

  // Path input box node in the navigation toolbar.
  std::shared_ptr<perception::ui::Node> path_label_;

  // Bottom status bar label node.
  std::shared_ptr<perception::ui::Node> status_label_;

  // Toolbar Back button node.
  std::shared_ptr<perception::ui::Node> back_button_;

  // Toolbar Forward button node.
  std::shared_ptr<perception::ui::Node> forward_button_;

  // Toolbar Up button node.
  std::shared_ptr<perception::ui::Node> up_button_;
};
