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
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include "note_tree.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/node.h"

class NotesWindow : public std::enable_shared_from_this<NotesWindow> {
 public:
  NotesWindow();
  ~NotesWindow() = default;

  void Initialize();
  std::shared_ptr<perception::ui::Node> GetWindowNode() const {
    return window_node_;
  }

 private:
  NoteTree tree_;
  std::shared_ptr<NoteNode> active_note_;
  std::set<int> expanded_note_ids_;
  bool tree_is_invalidated_ = false;
  int tree_mutation_version_ = 0;

  int last_clicked_note_id_ = -1;
  std::chrono::steady_clock::time_point last_click_time_;
  bool item_context_menu_shown_ = false;

  std::shared_ptr<perception::ui::Node> window_node_;
  std::shared_ptr<perception::ui::Node> active_dialog_;
  std::weak_ptr<perception::ui::Node> tree_container_node_;
  std::weak_ptr<perception::ui::Node> text_field_node_;

  std::unordered_map<int,
                     std::weak_ptr<perception::ui::components::TreeViewItem>>
      item_by_note_id_;
  std::unordered_map<perception::ui::components::TreeViewItem*,
                     std::shared_ptr<NoteNode>>
      note_by_item_ptr_;

  void RebuildTreeView();
  std::shared_ptr<perception::ui::Node> CreateTreeViewItemRecursive(
      std::shared_ptr<NoteNode> note);

  void SelectNote(std::shared_ptr<NoteNode> note);
  void LoadNoteIntoEditor(std::shared_ptr<NoteNode> note);
  void SwitchNoteWithUnsavedCheck(std::shared_ptr<NoteNode> target_note);

  void CreateNewNoteAtBottom();
  void CreateNestedNote(std::shared_ptr<NoteNode> parent_note);
  void DuplicateNote(std::shared_ptr<NoteNode> target_note);
  void RenameNote(std::shared_ptr<NoteNode> target_note);
  void PromptDeleteNote(std::shared_ptr<NoteNode> target_note);
  void DeleteNote(std::shared_ptr<NoteNode> target_note);

  void SaveActiveNoteContent();
  void ScheduleTreeSave();
  void SaveTreeNow();

  void UpdateNoteItemLabelFont(std::shared_ptr<NoteNode> note);
  void OnWindowClose();

  void ShowDialog(std::string_view title, std::string_view message,
                  std::string_view yes_button_text,
                  std::function<void()> on_yes,
                  std::string_view no_button_text = "",
                  std::function<void()> on_no = nullptr);
  void ShowRenameDialog(std::shared_ptr<NoteNode> note,
                        std::function<void()> on_renamed);
};
