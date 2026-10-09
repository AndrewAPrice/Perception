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

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "file_operations.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/window/keyboard_key_event.h"

// Callbacks emitted by FileListView in response to user interactions.
struct FileListCallbacks {
  // Invoked whenever the set of selected paths changes.
  std::function<void()> on_selection_changed;

  // Invoked when a single directory or file row is activated (e.g. double-click).
  std::function<void(const std::string& path, bool is_dir)> on_open_item;

  // Invoked when the current selection is opened via keyboard shortcut.
  std::function<void()> on_open_selected_items;

  // Invoked when a context menu is requested on a specific row.
  std::function<void(perception::ui::Node& context_node,
                     const perception::ui::Point& point,
                     const std::string& path, bool is_dir)>
      on_item_context_menu;

  // Invoked when a context menu is requested on the empty list background.
  std::function<void(perception::ui::Node& context_node,
                     const perception::ui::Point& point)>
      on_background_context_menu;

  // Invoked when the user clicks a column header to change sorting.
  std::function<void(SortColumn column, SortDirection direction)>
      on_sort_changed;

  // Invoked when the user triggers Copy (Ctrl+C).
  std::function<void()> on_copy;

  // Invoked when the user triggers Cut (Ctrl+X).
  std::function<void()> on_cut;

  // Invoked when the user triggers Paste (Ctrl+V).
  std::function<void()> on_paste;

  // Invoked when the user triggers Duplicate (Ctrl+D).
  std::function<void()> on_duplicate;

  // Invoked when the user triggers Rename (F2).
  std::function<void()> on_rename;

  // Invoked when the user triggers Delete (Delete key).
  std::function<void()> on_delete;

  // Invoked when the user triggers Refresh (F5 or Ctrl+R).
  std::function<void()> on_refresh;

  // Invoked when the user triggers Navigate Up (Backspace).
  std::function<void()> on_navigate_up;
};

// Manages the column headers, scrollable file rows, selection state, marquee
// drag selection, and keyboard navigation for File Manager.
class FileListView {
 public:
  // Constructs the file list view component with the given action callbacks.
  explicit FileListView(FileListCallbacks callbacks);

  // Returns the root UI node containing the column headers and scroll view.
  std::shared_ptr<perception::ui::Node> GetNode() const { return root_node_; }

  // Moves keyboard focus to the scrollable file list container.
  void Focus();

  // Updates the active sort column and direction shown in the column headers.
  void SetSortOrder(SortColumn column, SortDirection direction);

  // Replaces the displayed directory entries and selects any paths matching
  // `paths_to_select`.
  void SetEntries(std::vector<std::filesystem::directory_entry> entries,
                  const std::set<std::string>& paths_to_select);

  // Returns the currently displayed directory entries.
  const std::vector<std::filesystem::directory_entry>& GetEntries() const {
    return current_items_;
  }

  // Returns the currently selected item paths.
  const std::set<std::string>& GetSelectedPaths() const {
    return selected_paths_;
  }

  // Returns the selected item paths ordered as they appear in the list view.
  std::vector<std::string> GetSelectedPathsInDisplayOrder() const;

  // Selects all currently displayed items.
  void SelectAll();

 private:
  // Builds the column headers and scrollable list container nodes.
  void BuildUi();

  // Updates the text and colors of the column header labels.
  void UpdateHeaderLabels();

  // Updates row background fill colors based on selection and hover state.
  void UpdateRowVisuals();

  // Selects an item by row index, optionally extending the selection range from
  // the current anchor.
  void SelectByIndex(int new_index, bool extend_range);

  // Converts a local point within `source_node` to coordinates relative to the
  // files list container.
  perception::ui::Point PointInFilesList(
      perception::ui::Node& source_node,
      const perception::ui::Point& local_point) const;

  // Begins tracking a mouse drag inside the file list.
  void BeginListMouseDrag(perception::ui::Node& source_node,
                          const perception::ui::Point& local_point);

  // Updates marquee selection during an active mouse drag.
  void HandleListMouseDrag(perception::ui::Node& source_node,
                           const perception::ui::Point& local_point);

  // Ends an active mouse drag or marquee selection.
  void EndListMouseDrag();

  // Handles key press events when the file list is focused.
  void HandleKeyDown(const perception::window::KeyboardKeyEvent& event);

  // Handles key release events when the file list is focused.
  void HandleKeyUp(const perception::window::KeyboardKeyEvent& event);

  // Action and notification callbacks.
  FileListCallbacks callbacks_;

  // Active sort column.
  SortColumn sort_column_ = SortColumn::NAME;

  // Active sort direction.
  SortDirection sort_direction_ = SortDirection::ASCENDING;

  // Currently displayed directory entries.
  std::vector<std::filesystem::directory_entry> current_items_;

  // UI row nodes corresponding to `current_items_`.
  std::vector<std::shared_ptr<perception::ui::Node>> row_nodes_;

  // Set of currently selected filesystem paths.
  std::set<std::string> selected_paths_;

  // Anchor row index for Shift-range selection (-1 if none).
  int anchor_index_ = -1;

  // Focused row index for keyboard navigation (-1 if none).
  int focus_index_ = -1;

  // Currently hovered row index (-1 if none).
  int hovered_index_ = -1;

  // Whether a Control key is currently held.
  bool ctrl_pressed_ = false;

  // Whether a Shift key is currently held.
  bool shift_pressed_ = false;

  // Whether a row handled the current right-click context menu event.
  bool item_context_menu_shown_ = false;

  // Whether a row handled the current mouse button down event.
  bool row_mouse_down_handled_ = false;

  // Whether the left mouse button is currently pressed inside the list.
  bool is_mouse_down_in_list_ = false;

  // Whether a marquee drag selection is actively in progress.
  bool is_drag_selecting_ = false;

  // Starting point of the marquee drag in list coordinates.
  perception::ui::Point drag_start_in_list_{.x = 0.0f, .y = 0.0f};

  // Current point of the marquee drag in list coordinates.
  perception::ui::Point drag_current_in_list_{.x = 0.0f, .y = 0.0f};

  // Selection snapshot at the start of a modified marquee drag.
  std::set<std::string> drag_initial_selection_;

  // Path of the most recently clicked row for double-click detection.
  std::string last_clicked_path_;

  // Timestamp of the most recent row click for double-click detection.
  std::chrono::steady_clock::time_point last_click_time_;

  // Root container holding column headers and the scroll container.
  std::shared_ptr<perception::ui::Node> root_node_;

  // Scrollable container node for file rows.
  std::shared_ptr<perception::ui::Node> files_scroll_container_;

  // Vertical container node holding the individual file row nodes.
  std::shared_ptr<perception::ui::Node> files_list_container_;

  // Column header label for the Name column.
  std::shared_ptr<perception::ui::Node> name_header_label_;

  // Column header label for the Size column.
  std::shared_ptr<perception::ui::Node> size_header_label_;
};
