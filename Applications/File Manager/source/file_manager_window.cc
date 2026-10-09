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

#include "file_manager_window.h"

#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <system_error>

#include "dialogs.h"
#include "perception/clipboard.h"
#include "perception/file.h"
#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/message_dialog.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/image.h"
#include "perception/ui/layout.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"

using ::perception::Defer;
using ::perception::FormatSize;
using ::perception::GetService;
using ::perception::LoadApplicationRequest;
using ::perception::LoadApplicationResponse;
using ::perception::Loader;
using ::perception::SetClipboard;
using ::perception::TerminateProcess;
using ::perception::ui::Image;
using ::perception::ui::kTextBoxTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::ImageButton;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ShowMessageDialog;
using ::perception::ui::components::UiWindow;

namespace {

// Default File Manager window width.
constexpr float kWindowWidth = 440.0f;

// Height of the window title bar.
constexpr float kTitleBarHeight = 30.0f;

// Path to the back navigation icon asset.
constexpr std::string_view kBackIconPath =
    "/Applications/File Manager/back.svg";

// Path to the forward navigation icon asset.
constexpr std::string_view kForwardIconPath =
    "/Applications/File Manager/forward.svg";

// Path to the parent directory navigation icon asset.
constexpr std::string_view kUpIconPath = "/Applications/File Manager/up.svg";

// Path to the refresh directory icon asset.
constexpr std::string_view kRefreshIconPath =
    "/Applications/File Manager/refresh.svg";

// Left margin on the status bar label.
constexpr float kStatusMarginLeft = 4.0f;

// Secondary label color for status text.
constexpr uint32 kSecondaryLabelColor = 0xFF6B7280;

// Text color when an invalid directory path is entered.
constexpr uint32 kErrorTextColor = 0xFFB91C1C;

// Semi-transparent gray tint color for the application loading overlay.
constexpr uint32 kLoadingOverlayColor = 0x80808080;

}  // namespace

FileManagerWindow::FileManagerWindow(std::string_view initial_directory)
    : initial_directory_(initial_directory.empty()
                             ? "/"
                             : std::string(initial_directory)) {}

void FileManagerWindow::Initialize() {
  BuildUi();
  NavigateTo(initial_directory_, false);
  file_list_view_->Focus();
}

void FileManagerWindow::BuildUi() {
  FileListCallbacks callbacks;
  callbacks.on_selection_changed = [this]() { UpdateStatusLabel(); };
  callbacks.on_open_item = [this](const std::string& path, bool is_dir) {
    OpenItem(path, is_dir);
  };
  callbacks.on_open_selected_items = [this]() { OpenSelectedItems(); };
  callbacks.on_item_context_menu = [this](Node& context_node,
                                          const Point& point,
                                          const std::string& path,
                                          bool is_dir) {
    ShowContextMenuForItem(context_node, point, path, is_dir);
  };
  callbacks.on_background_context_menu = [this](Node& context_node,
                                                const Point& point) {
    ShowContextMenuForBackground(context_node, point);
  };
  callbacks.on_sort_changed = [this](SortColumn col, SortDirection dir) {
    sort_column_ = col;
    sort_direction_ = dir;
    NavigateTo(current_path_, false, file_list_view_->GetSelectedPaths());
  };
  callbacks.on_copy = [this]() { CopySelectedItems(); };
  callbacks.on_cut = [this]() { CutSelectedItems(); };
  callbacks.on_paste = [this]() { PasteClipboardItems(); };
  callbacks.on_duplicate = [this]() { DuplicateSelectedItems(); };
  callbacks.on_rename = [this]() { PromptRenameSelectedItem(); };
  callbacks.on_delete = [this]() { PromptDeleteSelectedItems(); };
  callbacks.on_refresh = [this]() {
    NavigateTo(current_path_, false, file_list_view_->GetSelectedPaths());
  };
  callbacks.on_navigate_up = [this]() { GoUp(); };

  file_list_view_ = std::make_unique<FileListView>(std::move(callbacks));

  main_window_ = UiWindow::ResizableWindowWithTitleBar(
      "File Manager",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      [](Layout& layout) { layout.SetWidth(kWindowWidth); },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
          },
          // Navigation toolbar (Back, Forward, Up, Path InputBox, Refresh)
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetAlignItems(YGAlignCenter);
              },
              ImageButton::BasicImageButton(
                  [this]() { Defer([this]() { GoBack(); }); },
                  Image::LoadImage(kBackIconPath),
                  [](Button& button) { button.SetEnabled(false); },
                  &back_button_),
              ImageButton::BasicImageButton(
                  [this]() { Defer([this]() { GoForward(); }); },
                  Image::LoadImage(kForwardIconPath),
                  [](Button& button) { button.SetEnabled(false); },
                  &forward_button_),
              ImageButton::BasicImageButton(
                  [this]() { Defer([this]() { GoUp(); }); },
                  Image::LoadImage(kUpIconPath),
                  [](Button& button) { button.SetEnabled(false); },
                  &up_button_),
              InputBox::BasicInputBox(
                  "/",
                  [this](InputBox& input_box) {
                    auto* input_ptr = &input_box;
                    input_box.OnEnterPressed(
                        [this, input_ptr](std::string_view text) {
                          std::string path_str = std::string(text);
                          struct stat st;
                          if (stat(path_str.c_str(), &st) != 0 ||
                              !S_ISDIR(st.st_mode)) {
                            input_ptr->SetTextColor(kErrorTextColor);
                          } else {
                            input_ptr->SetTextColor(kTextBoxTextColor);
                            Defer([this, path_str]() {
                              NavigateTo(path_str, true);
                              file_list_view_->Focus();
                            });
                          }
                        });
                  },
                  [](Layout& layout) {
                    layout.SetFlexGrow(1.0f);
                    layout.SetFlexShrink(1.0f);
                    layout.SetMinWidth(0.0f);
                  },
                  &path_label_),
              ImageButton::BasicImageButton(
                  [this]() {
                    Defer([this]() {
                      NavigateTo(current_path_, false,
                                 file_list_view_->GetSelectedPaths());
                    });
                  },
                  Image::LoadImage(kRefreshIconPath))),
          file_list_view_->GetNode(),
          // Status label
          Label::BasicLabel(
              "0 items",
              [](Layout& layout) {
                layout.SetMargin(YGEdgeLeft, kStatusMarginLeft);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(kSecondaryLabelColor);
              },
              &status_label_)),
      Node::Empty(
          [](Layout& layout) {
            layout.SetPositionType(YGPositionTypeAbsolute);
            layout.SetPosition(YGEdgeLeft, 0.0f);
            layout.SetPosition(YGEdgeTop, kTitleBarHeight);
            layout.SetPosition(YGEdgeRight, 0.0f);
            layout.SetPosition(YGEdgeBottom, 0.0f);
            layout.SetDisplay(YGDisplayNone);
          },
          [](Block& block) { block.SetFillColor(kLoadingOverlayColor); },
          [](Node& node) { node.SetBlocksHitTest(true); },
          &loading_overlay_));
}

void FileManagerWindow::NavigateTo(
    const std::string& path, bool record_history,
    const std::set<std::string>& paths_to_select) {
  std::string target_path = NormalizeAndResolveDirectoryPath(path);

  if (record_history && !current_path_.empty() &&
      target_path != current_path_) {
    back_history_.push_back(current_path_);
    forward_history_.clear();
  }

  current_path_ = target_path;

  if (path_label_) {
    auto input_box = path_label_->Get<InputBox>();
    input_box->SetText(current_path_);
    input_box->SetTextColor(kTextBoxTextColor);
  }

  UpdateNavigationButtons();

  auto entries = ReadDirectoryEntries(current_path_, show_hidden_files_,
                                      sort_column_, sort_direction_);
  file_list_view_->SetSortOrder(sort_column_, sort_direction_);
  file_list_view_->SetEntries(std::move(entries), paths_to_select);
}

void FileManagerWindow::GoBack() {
  if (back_history_.empty()) return;
  std::string target = back_history_.back();
  back_history_.pop_back();
  forward_history_.push_back(current_path_);
  NavigateTo(target, false);
}

void FileManagerWindow::GoForward() {
  if (forward_history_.empty()) return;
  std::string target = forward_history_.back();
  forward_history_.pop_back();
  back_history_.push_back(current_path_);
  NavigateTo(target, false);
}

void FileManagerWindow::GoUp() {
  if (current_path_ == "/") return;
  std::filesystem::path p(current_path_);
  std::string parent = p.parent_path().string();
  if (parent.empty()) parent = "/";
  NavigateTo(parent, true);
}

void FileManagerWindow::UpdateNavigationButtons() {
  auto update_btn = [](const std::shared_ptr<Node>& btn_node, bool enabled) {
    if (!btn_node) return;
    if (auto button = btn_node->Get<Button>()) button->SetEnabled(enabled);
    btn_node->Invalidate();
  };

  update_btn(back_button_, !back_history_.empty());
  update_btn(forward_button_, !forward_history_.empty());
  update_btn(up_button_, current_path_ != "/");
}

void FileManagerWindow::UpdateStatusLabel() {
  if (!status_label_ || !file_list_view_) return;
  size_t total = file_list_view_->GetEntries().size();
  std::string status_text =
      std::to_string(total) + (total == 1 ? " item" : " items");

  const auto& selected_paths = file_list_view_->GetSelectedPaths();
  if (selected_paths.size() == 1) {
    std::string sel_path = *selected_paths.begin();
    std::string sel_name = std::filesystem::path(sel_path).filename().string();
    status_text += " — Selected: " + sel_name;
    std::error_code ec;
    if (!std::filesystem::is_directory(sel_path, ec)) {
      uintmax_t sz = std::filesystem::file_size(sel_path, ec);
      if (!ec) status_text += " (" + FormatSize(sz) + ")";
    }
  } else if (selected_paths.size() > 1) {
    status_text +=
        " (" + std::to_string(selected_paths.size()) + " selected)";
  }

  status_label_->Get<Label>()->SetText(status_text);
  status_label_->Invalidate();
}

void FileManagerWindow::OpenItem(const std::string& entry_path, bool is_dir) {
  if (is_dir) {
    Defer([this, entry_path]() { NavigateTo(entry_path, true); });
  } else {
    LaunchFileOrApplication(entry_path);
  }
}

void FileManagerWindow::LaunchFileOrApplication(const std::string& entry_path) {
  pending_launches_++;
  UpdateLoadingOverlay();

  LoadApplicationRequest request;
  request.name = entry_path;
  GetService<Loader>().LaunchApplication(
      request, [this](StatusOr<LoadApplicationResponse> response) {
        if (pending_launches_ > 0) pending_launches_--;
        UpdateLoadingOverlay();
      });
}

void FileManagerWindow::UpdateLoadingOverlay() {
  if (!loading_overlay_) return;
  loading_overlay_->GetLayout().SetDisplay(
      pending_launches_ > 0 ? YGDisplayFlex : YGDisplayNone);
  loading_overlay_->Invalidate();
  if (main_window_) main_window_->Invalidate();
}

void FileManagerWindow::OpenSelectedItems() {
  const auto& selected_paths = file_list_view_->GetSelectedPaths();
  if (selected_paths.empty()) return;
  if (selected_paths.size() == 1) {
    std::string path = *selected_paths.begin();
    std::error_code ec;
    bool is_dir = std::filesystem::is_directory(path, ec);
    OpenItem(path, is_dir);
    return;
  }

  for (const auto& path : file_list_view_->GetSelectedPathsInDisplayOrder()) {
    std::error_code ec;
    if (!std::filesystem::is_directory(path, ec))
      LaunchFileOrApplication(path);
  }
}

void FileManagerWindow::CopySelectedItems() {
  if (file_list_view_->GetSelectedPaths().empty()) return;
  file_clipboard_paths_ = file_list_view_->GetSelectedPathsInDisplayOrder();
  file_clipboard_is_cut_ = false;
}

void FileManagerWindow::CutSelectedItems() {
  if (file_list_view_->GetSelectedPaths().empty()) return;
  file_clipboard_paths_ = file_list_view_->GetSelectedPathsInDisplayOrder();
  file_clipboard_is_cut_ = true;
}

void FileManagerWindow::PasteClipboardItems() {
  if (file_clipboard_paths_.empty() || current_path_ == "/") return;

  std::set<std::string> newly_created;
  std::error_code ec;

  for (const auto& src_path : file_clipboard_paths_) {
    if (!std::filesystem::exists(src_path, ec)) continue;

    bool src_is_dir = std::filesystem::is_directory(src_path, ec);
    if (src_is_dir) {
      std::string src_prefix =
          src_path.back() == '/' ? src_path : src_path + "/";
      if (current_path_ == src_path ||
          current_path_.rfind(src_prefix, 0) == 0) {
        ShowMessageDialog("Cannot Paste",
                          "Cannot copy or move a folder into itself.",
                          main_window_);
        continue;
      }
    }

    std::string filename = std::filesystem::path(src_path).filename().string();
    std::string src_parent =
        std::filesystem::path(src_path).parent_path().string();
    if (src_parent.empty()) src_parent = "/";

    if (file_clipboard_is_cut_ && src_parent == current_path_) {
      newly_created.insert(src_path);
      continue;
    }

    std::string dest_path = GetUniquePath(current_path_, filename);
    std::string error_message;
    bool ok = file_clipboard_is_cut_
                  ? MoveOrRenameItem(src_path, dest_path, error_message)
                  : CopyItemRecursive(src_path, dest_path, error_message);

    if (!ok) {
      ShowMessageDialog("Paste Error", error_message, main_window_);
    } else {
      newly_created.insert(dest_path);
    }
  }

  if (file_clipboard_is_cut_) {
    file_clipboard_paths_.clear();
    file_clipboard_is_cut_ = false;
  }

  NavigateTo(current_path_, false, newly_created);
}

void FileManagerWindow::DuplicateSelectedItems() {
  if (file_list_view_->GetSelectedPaths().empty() || current_path_ == "/")
    return;
  auto ordered = file_list_view_->GetSelectedPathsInDisplayOrder();
  std::set<std::string> duplicated;

  for (const auto& src_path : ordered) {
    std::error_code ec;
    if (!std::filesystem::exists(src_path, ec)) continue;
    std::string filename = std::filesystem::path(src_path).filename().string();
    std::string dest_path = GetUniquePath(current_path_, filename, true);
    std::string error_message;
    if (!CopyItemRecursive(src_path, dest_path, error_message)) {
      ShowMessageDialog("Duplicate Error", error_message, main_window_);
    } else {
      duplicated.insert(dest_path);
    }
  }

  NavigateTo(current_path_, false, duplicated);
}

void FileManagerWindow::CopySelectedPathsToClipboard(
    const std::string& fallback_path) {
  auto ordered = file_list_view_->GetSelectedPathsInDisplayOrder();
  if (ordered.empty()) {
    SetClipboard(fallback_path);
    return;
  }
  std::string combined;
  for (size_t i = 0; i < ordered.size(); i++) {
    if (i > 0) combined += "\n";
    combined += ordered[i];
  }
  SetClipboard(combined);
}

void FileManagerWindow::PromptCreateNewFolder() {
  if (current_path_ == "/") return;
  std::string default_path = GetUniquePath(current_path_, "New Folder");
  std::string default_name =
      std::filesystem::path(default_path).filename().string();

  ShowTextInputDialog(
      "New Folder", "Folder name:", default_name, "Create", main_window_,
      [this](std::string folder_name) {
        std::string target = JoinPath(current_path_, folder_name);
        std::error_code ec;
        if (std::filesystem::exists(target, ec)) {
          ShowMessageDialog("Cannot Create Folder",
                            "An item with that name already exists.",
                            main_window_);
          return;
        }
        if (!std::filesystem::create_directory(target, ec) && ec) {
          ShowMessageDialog("Cannot Create Folder", ec.message(),
                            main_window_);
          return;
        }
        NavigateTo(current_path_, false, {target});
      },
      [this]() { file_list_view_->Focus(); });
}

void FileManagerWindow::PromptCreateNewFile() {
  if (current_path_ == "/") return;
  std::string default_path = GetUniquePath(current_path_, "Untitled.txt");
  std::string default_name =
      std::filesystem::path(default_path).filename().string();

  ShowTextInputDialog(
      "New File", "File name:", default_name, "Create", main_window_,
      [this](std::string file_name) {
        std::string target = JoinPath(current_path_, file_name);
        std::error_code ec;
        if (std::filesystem::exists(target, ec)) {
          ShowMessageDialog("Cannot Create File",
                            "An item with that name already exists.",
                            main_window_);
          return;
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
          ShowMessageDialog("Cannot Create File",
                            "Failed to create the requested file.",
                            main_window_);
          return;
        }
        out.close();
        NavigateTo(current_path_, false, {target});
      },
      [this]() { file_list_view_->Focus(); });
}

void FileManagerWindow::PromptRenameSelectedItem() {
  const auto& selected_paths = file_list_view_->GetSelectedPaths();
  if (selected_paths.size() != 1 || current_path_ == "/") return;
  std::string old_path = *selected_paths.begin();
  std::string old_name = std::filesystem::path(old_path).filename().string();

  ShowTextInputDialog(
      "Rename", "New name:", old_name, "Rename", main_window_,
      [this, old_path, old_name](std::string new_name) {
        if (new_name == old_name) return;
        std::string new_path = JoinPath(current_path_, new_name);
        std::error_code ec;
        if (std::filesystem::exists(new_path, ec)) {
          ShowMessageDialog("Cannot Rename",
                            "An item with that name already exists.",
                            main_window_);
          return;
        }
        std::string error_message;
        if (!MoveOrRenameItem(old_path, new_path, error_message)) {
          ShowMessageDialog("Cannot Rename", error_message, main_window_);
          return;
        }
        NavigateTo(current_path_, false, {new_path});
      },
      [this]() { file_list_view_->Focus(); });
}

void FileManagerWindow::PromptDeleteSelectedItems() {
  const auto& selected_paths = file_list_view_->GetSelectedPaths();
  if (selected_paths.empty() || current_path_ == "/") return;

  std::vector<std::string> targets(selected_paths.begin(),
                                   selected_paths.end());
  ShowDeleteConfirmationDialog(
      targets, main_window_,
      [this, targets]() {
        std::string error_message;
        for (const auto& target : targets) {
          if (!DeleteItemRecursive(target, error_message)) {
            ShowMessageDialog("Delete Error", error_message, main_window_);
            break;
          }
        }
        NavigateTo(current_path_, false);
      },
      [this]() { file_list_view_->Focus(); });
}

void FileManagerWindow::ShowContextMenuForItem(Node& context_node,
                                               const Point& point,
                                               const std::string& entry_path,
                                               bool is_dir) {
  bool is_writable_dir = current_path_ != "/";
  bool single_selected =
      is_writable_dir && file_list_view_->GetSelectedPaths().size() <= 1;
  bool can_paste = is_writable_dir && !file_clipboard_paths_.empty();

  auto menu = PopUpMenu::Container(
      PopUpMenu::ItemWithIconAndShortcut(
          "Open", nullptr, "Enter",
          [this, entry_path, is_dir]() {
            if (file_list_view_->GetSelectedPaths().size() > 1) {
              OpenSelectedItems();
            } else {
              OpenItem(entry_path, is_dir);
            }
          }),
      PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut(
          "Cut", nullptr, "Ctrl+X", [this]() { CutSelectedItems(); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Copy", nullptr, "Ctrl+C", [this]() { CopySelectedItems(); }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Paste", nullptr, "Ctrl+V",
          [this]() { Defer([this]() { PasteClipboardItems(); }); },
          [can_paste](Button& btn) { btn.SetEnabled(can_paste); }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Duplicate", nullptr, "Ctrl+D",
          [this]() { Defer([this]() { DuplicateSelectedItems(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::ContextMenuItem(
          "Copy path",
          [this, entry_path]() { CopySelectedPathsToClipboard(entry_path); }),
      PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut(
          "Rename...", nullptr, "F2",
          [this]() { Defer([this]() { PromptRenameSelectedItem(); }); },
          [single_selected](Button& btn) { btn.SetEnabled(single_selected); }),
      PopUpMenu::ContextMenuItem(
          "New Folder...",
          [this]() { Defer([this]() { PromptCreateNewFolder(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::ContextMenuItem(
          "New File...",
          [this]() { Defer([this]() { PromptCreateNewFile(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::Divider(),
      PopUpMenu::ItemWithIconAndShortcut(
          "Delete...", nullptr, "Del",
          [this]() { Defer([this]() { PromptDeleteSelectedItems(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }));

  PopUp::Show(context_node.shared_from_this(),
              context_node.GetAbsolutePosition() + point, menu);
}

void FileManagerWindow::ShowContextMenuForBackground(Node& context_node,
                                                     const Point& point) {
  bool is_writable_dir = current_path_ != "/";
  bool can_paste = is_writable_dir && !file_clipboard_paths_.empty();
  std::string hidden_label =
      show_hidden_files_ ? "Hide Hidden Files" : "Show Hidden Files";

  auto menu = PopUpMenu::Container(
      PopUpMenu::ContextMenuItem(
          "New Folder...",
          [this]() { Defer([this]() { PromptCreateNewFolder(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::ContextMenuItem(
          "New File...",
          [this]() { Defer([this]() { PromptCreateNewFile(); }); },
          [is_writable_dir](Button& btn) { btn.SetEnabled(is_writable_dir); }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Paste", nullptr, "Ctrl+V",
          [this]() { Defer([this]() { PasteClipboardItems(); }); },
          [can_paste](Button& btn) { btn.SetEnabled(can_paste); }),
      PopUpMenu::ContextMenuItem("Copy path",
                                 [this]() { SetClipboard(current_path_); }),
      PopUpMenu::Divider(),
      PopUpMenu::ContextMenuItem(
          hidden_label,
          [this]() {
            show_hidden_files_ = !show_hidden_files_;
            Defer([this]() {
              NavigateTo(current_path_, false,
                         file_list_view_->GetSelectedPaths());
            });
          }),
      PopUpMenu::ItemWithIconAndShortcut(
          "Refresh", nullptr, "F5", [this]() {
            Defer([this]() {
              NavigateTo(current_path_, false,
                         file_list_view_->GetSelectedPaths());
            });
          }));

  PopUp::Show(context_node.shared_from_this(),
              context_node.GetAbsolutePosition() + point, menu);
}
