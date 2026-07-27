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

#include "notes_window.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "perception/processes.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/text_field.h"
#include "perception/ui/components/tree_view.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::TerminateProcess;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::GetBook12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::Node;

using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::TextField;
using ::perception::ui::components::TreeView;
using ::perception::ui::components::TreeViewItem;
using ::perception::ui::components::UiWindow;

namespace {

int opened_instances = 0;

}  // namespace

NotesWindow::NotesWindow() {}

void NotesWindow::Initialize() {
  opened_instances++;

  LoadNotesTreeFromDisk(tree_);
  tree_.EnsureAtLeastOneNote();

  auto tree_view_node = TreeView::Create();
  tree_container_node_ = tree_view_node;

  window_node_ = UiWindow::ResizableWindowWithTitleBar(
      "Notes",
      [](Layout& layout) {
        layout.SetWidth(750.0f);
        layout.SetHeight(500.0f);
      },
      [this](UiWindow& window) {
        window.OnClose([this]() { OnWindowClose(); });
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetMinWidth(0.0f);
          },
          // Main Body
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetMinHeight(0.0f);
                layout.SetMinWidth(0.0f);
              },
              // Left Tree Panel
              Container::VerticalContainer(
                  [](Layout& layout) {
                    layout.SetWidth(240.0f);
                    layout.SetHeightPercent(100.0f);
                    layout.SetFlexShrink(0.0f);
                  },
                  tree_view_node),
              // Right Editor Panel
              TextField::BasicTextField(
                  "",
                  [](Layout& layout) {
                    layout.SetFlexGrow(1.0f);
                    layout.SetFlexShrink(1.0f);
                    layout.SetMinHeight(0.0f);
                    layout.SetMinWidth(0.0f);
                  },
                  &text_field_node_))));

  if (auto tf_node = text_field_node_.lock()) {
    if (auto tf = tf_node->Get<TextField>()) {
      tf->SetWordWrap(true);
      tf->OnTextChanged([this](std::string_view) {
        if (active_note_) {
          if (!active_note_->is_dirty) {
            active_note_->is_dirty = true;
            UpdateNoteItemLabelFont(active_note_);
          }
        }
      });
    }
  }

  if (auto tv = tree_view_node->Get<TreeView>()) {
    tv->OnDrop([this](std::shared_ptr<TreeViewItem> source_item,
                      std::shared_ptr<TreeViewItem> target_item,
                      ::perception::ui::components::TreeViewDropPosition
                          position) {
      auto source_note = note_by_item_ptr_[source_item.get()];
      if (!source_note) return;

      tree_.RemoveNote(source_note->id);

      if (position ==
          ::perception::ui::components::TreeViewDropPosition::ON_TOP) {
        if (target_item) {
          auto target_note = note_by_item_ptr_[target_item.get()];
          if (!target_note) return;
          source_note->parent = target_note;
          target_note->children.push_back(source_note);
        } else {
          source_note->parent.reset();
          tree_.root_notes.push_back(source_note);
        }
      } else {
        auto target_note =
            target_item ? note_by_item_ptr_[target_item.get()] : nullptr;
        if (target_note) {
          auto parent_note = target_note->parent.lock();
          auto& list = parent_note ? parent_note->children : tree_.root_notes;
          auto it = std::find(list.begin(), list.end(), target_note);
          if (position ==
                  ::perception::ui::components::TreeViewDropPosition::AFTER &&
              it != list.end()) {
            ++it;
          }
          list.insert(it, source_note);
          source_note->parent = parent_note;
          if (parent_note) expanded_note_ids_.insert(parent_note->id);
        } else {
          if (position ==
              ::perception::ui::components::TreeViewDropPosition::BEFORE) {
            tree_.root_notes.insert(tree_.root_notes.begin(), source_note);
          } else {
            tree_.root_notes.push_back(source_note);
          }
          source_note->parent.reset();
        }
      }

      ScheduleTreeSave();
      RebuildTreeView();
    });
  }

  tree_view_node->OnMouseButtonDown(
      [this, tree_view_node](const perception::ui::Point& pt,
                             ::perception::window::MouseButton button) {
        if (button == ::perception::window::MouseButton::Right) {
          if (item_context_menu_shown_) {
            item_context_menu_shown_ = false;
            return;
          }
          perception::ui::Point screen_pt =
              tree_view_node->GetAbsolutePosition() + pt;
          auto menu = PopUpMenu::Container(PopUpMenu::ContextMenuItem(
              "New note", [this]() { CreateNewNoteAtBottom(); }));
          PopUp::Show(window_node_, screen_pt, menu);
        } else {
          item_context_menu_shown_ = false;
        }
      });

  RebuildTreeView();

  if (!tree_.root_notes.empty()) {
    SelectNote(tree_.root_notes.front());
  }
}

void NotesWindow::RebuildTreeView() {
  for (const auto& [id, item_weak] : item_by_note_id_) {
    if (auto item = item_weak.lock()) {
      if (item->IsExpanded()) {
        expanded_note_ids_.insert(id);
      } else {
        expanded_note_ids_.erase(id);
      }
    }
  }
  item_by_note_id_.clear();
  note_by_item_ptr_.clear();

  auto tv_node = tree_container_node_.lock();
  if (!tv_node) return;

  auto tv = tv_node->Get<TreeView>();
  if (!tv) return;

  auto content_container = tv->GetContentContainer();
  if (!content_container) return;

  content_container->RemoveChildren();

  for (const auto& root_note : tree_.root_notes) {
    auto item_node = CreateTreeViewItemRecursive(root_note);
    if (item_node) {
      content_container->AddChild(item_node);
    }
  }

  tv_node->Invalidate();
}

std::shared_ptr<Node> NotesWindow::CreateTreeViewItemRecursive(
    std::shared_ptr<NoteNode> note) {
  if (!note) return nullptr;

  std::vector<std::shared_ptr<Node>> child_item_nodes;
  for (const auto& child : note->children) {
    auto child_item = CreateTreeViewItemRecursive(child);
    if (child_item) child_item_nodes.push_back(child_item);
  }

  auto label = Label::BasicLabel(note->name, [note](Label& l) {
    l.SetColor(::perception::ui::kTreeViewItemTextColor);
    l.SetFont(note->is_dirty ? GetBold12UiFont() : GetBook12UiFont());
  });

  auto item_node = TreeViewItem::Item(label, child_item_nodes);
  auto item = item_node->Get<TreeViewItem>();

  if (item) {
    item_by_note_id_[note->id] = item;
    note_by_item_ptr_[item.get()] = note;

    if (expanded_note_ids_.count(note->id)) item->SetExpanded(true);
    item->OnToggle([this, id = note->id](bool expanded) {
      if (expanded) {
        expanded_note_ids_.insert(id);
      } else {
        expanded_note_ids_.erase(id);
      }
    });

    item->OnSelect([this, note]() { SwitchNoteWithUnsavedCheck(note); });

    if (auto content = item->GetContentContainer()) {
      content->OnMouseButtonDown([this, note](
                                     const perception::ui::Point&,
                                     ::perception::window::MouseButton button) {
        if (button == ::perception::window::MouseButton::Left) {
          auto now = std::chrono::steady_clock::now();
          auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
              now - last_click_time_);
          if (last_clicked_note_id_ == note->id && duration.count() < 500) {
            last_clicked_note_id_ = -1;
            RenameNote(note);
          } else {
            last_clicked_note_id_ = note->id;
            last_click_time_ = now;
          }
        }
      });
    }

    item->OnContextMenu([this, note](perception::ui::Point screen_pt) {
      item_context_menu_shown_ = true;
      auto menu = PopUpMenu::Container(
          PopUpMenu::ContextMenuItem(
              "New nested note", [this, note]() { CreateNestedNote(note); }),
          PopUpMenu::ContextMenuItem("Duplicate note",
                                     [this, note]() { DuplicateNote(note); }),
          PopUpMenu::ContextMenuItem("Rename note",
                                     [this, note]() { RenameNote(note); }),
          PopUpMenu::ContextMenuItem(
              "Delete note", [this, note]() { PromptDeleteNote(note); }));

      PopUp::Show(window_node_, screen_pt, menu);
    });
  }

  return item_node;
}

void NotesWindow::SelectNote(std::shared_ptr<NoteNode> note) {
  if (!note) return;
  active_note_ = note;
  LoadNoteIntoEditor(note);

  auto item_weak = item_by_note_id_[note->id];
  if (auto item = item_weak.lock()) {
    item->Select(true, false);
  }
}

void NotesWindow::LoadNoteIntoEditor(std::shared_ptr<NoteNode> note) {
  if (!note) return;
  std::string content = LoadNoteContentFromDisk(note->id);

  if (auto tf_node = text_field_node_.lock()) {
    if (auto tf = tf_node->Get<TextField>()) {
      tf->SetText(content);
    }
  }

  note->is_dirty = false;
  UpdateNoteItemLabelFont(note);
}

void NotesWindow::SwitchNoteWithUnsavedCheck(
    std::shared_ptr<NoteNode> target_note) {
  if (!target_note) return;
  if (active_note_ == target_note) return;

  if (active_note_ && active_note_->is_dirty) {
    ShowDialog(
        "Save Changes", "Save changes to notes?", "Yes",
        [this, target_note]() {
          SaveActiveNoteContent();
          SelectNote(target_note);
        },
        "No",
        [this, target_note]() {
          active_note_->is_dirty = false;
          UpdateNoteItemLabelFont(active_note_);
          SelectNote(target_note);
        });
  } else {
    SelectNote(target_note);
  }
}

void NotesWindow::CreateNewNoteAtBottom() {
  auto new_note = std::make_shared<NoteNode>();
  new_note->id = tree_.next_id++;
  new_note->name = "Untitled note";
  new_note->is_dirty = false;

  tree_.root_notes.push_back(new_note);
  SaveNoteContentToDisk(new_note->id, "");

  ScheduleTreeSave();
  RebuildTreeView();
  SwitchNoteWithUnsavedCheck(new_note);
}

void NotesWindow::CreateNestedNote(std::shared_ptr<NoteNode> parent_note) {
  if (!parent_note) return;

  auto child_note = std::make_shared<NoteNode>();
  child_note->id = tree_.next_id++;
  child_note->name = "Untitled note";
  child_note->is_dirty = false;
  child_note->parent = parent_note;

  parent_note->children.push_back(child_note);
  SaveNoteContentToDisk(child_note->id, "");

  ScheduleTreeSave();
  RebuildTreeView();

  if (auto parent_item = item_by_note_id_[parent_note->id].lock()) {
    parent_item->SetExpanded(true);
  }

  SwitchNoteWithUnsavedCheck(child_note);
}

void NotesWindow::DuplicateNote(std::shared_ptr<NoteNode> target_note) {
  if (!target_note) return;

  int new_id = 0;
  auto clone = tree_.DuplicateNote(target_note, new_id);

  ScheduleTreeSave();
  RebuildTreeView();

  if (clone) {
    SwitchNoteWithUnsavedCheck(clone);
  }
}

void NotesWindow::RenameNote(std::shared_ptr<NoteNode> target_note) {
  if (!target_note) return;

  ShowRenameDialog(target_note, [this, target_note]() {
    ScheduleTreeSave();
    RebuildTreeView();
    if (active_note_ == target_note) {
      SelectNote(target_note);
    }
  });
}

void NotesWindow::PromptDeleteNote(std::shared_ptr<NoteNode> target_note) {
  if (!target_note) return;

  ShowDialog(
      "Delete Note", "Are you sure you want to delete this note?", "Yes",
      [this, target_note]() { DeleteNote(target_note); }, "No", nullptr);
}

void NotesWindow::DeleteNote(std::shared_ptr<NoteNode> target_note) {
  if (!target_note) return;

  DeleteNoteFilesFromDisk(target_note);
  tree_.RemoveNote(target_note->id);
  tree_.EnsureAtLeastOneNote();

  ScheduleTreeSave();
  RebuildTreeView();

  if (!active_note_ || tree_.FindNoteById(active_note_->id) == nullptr) {
    active_note_ = nullptr;
    if (auto tf_node = text_field_node_.lock()) {
      if (auto tf = tf_node->Get<TextField>()) {
        tf->SetText("");
      }
    }
    if (!tree_.root_notes.empty()) {
      SelectNote(tree_.root_notes.front());
    }
  }
}

void NotesWindow::SaveActiveNoteContent() {
  if (!active_note_) return;

  std::string text = "";
  if (auto tf_node = text_field_node_.lock()) {
    if (auto tf = tf_node->Get<TextField>()) {
      text = tf->GetText();
    }
  }

  SaveNoteContentToDisk(active_note_->id, text);
  active_note_->is_dirty = false;
  UpdateNoteItemLabelFont(active_note_);
}

void NotesWindow::ScheduleTreeSave() {
  tree_is_invalidated_ = true;
  int current_version = ++tree_mutation_version_;

  ::perception::AfterDuration(
      std::chrono::seconds(5), [this, current_version]() {
        if (current_version == tree_mutation_version_ && tree_is_invalidated_) {
          SaveTreeNow();
        }
      });
}

void NotesWindow::SaveTreeNow() {
  SaveNotesTreeToDisk(tree_);
  tree_is_invalidated_ = false;
}

void NotesWindow::UpdateNoteItemLabelFont(std::shared_ptr<NoteNode> note) {
  if (!note) return;

  auto item_weak = item_by_note_id_[note->id];
  auto item = item_weak.lock();
  if (!item) return;

  auto content_container = item->GetContentContainer();
  if (!content_container) return;

  for (const auto& child : content_container->GetChildren()) {
    if (auto label = child->Get<Label>()) {
      label->SetFont(note->is_dirty ? GetBold12UiFont() : GetBook12UiFont());
      break;
    }
  }
}

void NotesWindow::OnWindowClose() {
  auto close = [this]() {
    if (tree_is_invalidated_) SaveTreeNow();

    opened_instances--;
    if (opened_instances == 0) TerminateProcess();
  };

  if (active_note_ && active_note_->is_dirty) {
    ShowDialog(
        "Save Changes", "Save changes to notes?", "Yes",
        [this, close]() {
          SaveActiveNoteContent();
          close();
        },
        "No", [close]() { close(); });
  } else {
    close();
  }
}

void NotesWindow::ShowDialog(std::string_view title, std::string_view message,
                             std::string_view yes_button_text,
                             std::function<void()> on_yes,
                             std::string_view no_button_text,
                             std::function<void()> on_no) {
  if (active_dialog_) {
    active_dialog_->Get<UiWindow>()->Close();
    active_dialog_.reset();
  }

  active_dialog_ = UiWindow::DialogWithTitleBar(
      title, [](Layout& layout) { layout.SetWidth(340.0f); },
      [this](UiWindow& win) {
        win.OnClose([this]() { active_dialog_.reset(); });
      },
      Container::VerticalContainer(
          Label::BasicLabel(message),
          Container::HorizontalContainer(
              [](Layout& layout) { layout.SetJustifyContent(YGJustifyCenter); },
              Button::TextButton(yes_button_text,
                                 [this, on_yes]() {
                                   if (on_yes) on_yes();
                                   if (active_dialog_) {
                                     active_dialog_->Get<UiWindow>()->Close();
                                     active_dialog_.reset();
                                   }
                                 }),
              no_button_text.empty()
                  ? nullptr
                  : Button::TextButton(no_button_text, [this, on_no]() {
                      if (on_no) on_no();
                      if (active_dialog_) {
                        active_dialog_->Get<UiWindow>()->Close();
                        active_dialog_.reset();
                      }
                    }))));
}

void NotesWindow::ShowRenameDialog(std::shared_ptr<NoteNode> note,
                                   std::function<void()> on_renamed) {
  if (active_dialog_) {
    active_dialog_->Get<UiWindow>()->Close();
    active_dialog_.reset();
  }

  std::shared_ptr<Node> input_box_node;
  active_dialog_ = UiWindow::DialogWithTitleBar(
      "Rename Note", [](Layout& layout) { layout.SetWidth(340.0f); },
      [this](UiWindow& win) {
        win.OnClose([this]() { active_dialog_.reset(); });
      },
      Container::VerticalContainer(
          [](Layout& layout) { layout.SetAlignItems(YGAlignStretch); },
          Label::BasicLabel("Enter new note name:"),
          InputBox::BasicInputBox(
              note->name,
              [](Layout& layout) { layout.SetWidthPercent(100.0f); },
              &input_box_node),
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetJustifyContent(YGJustifyFlexEnd);
              },
              Button::TextButton("Cancel",
                                 [this]() {
                                   if (active_dialog_) {
                                     active_dialog_->Get<UiWindow>()->Close();
                                     active_dialog_.reset();
                                   }
                                 }),
              Button::TextButton(
                  "Rename", [this, note, input_box_node, on_renamed]() {
                    if (input_box_node) {
                      if (auto input_box = input_box_node->Get<InputBox>()) {
                        std::string new_name = input_box->GetText();
                        if (!new_name.empty()) {
                          note->name = new_name;
                          if (on_renamed) on_renamed();
                        }
                      }
                    }
                    if (active_dialog_) {
                      active_dialog_->Get<UiWindow>()->Close();
                      active_dialog_.reset();
                    }
                  }))));
}
