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

#include "perception/ui/components/file_dialog.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "perception/loader.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/file_icon.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

using ::perception::ui::GetBold12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;
using ::perception::window::MouseButton;

namespace perception {
namespace ui {
namespace components {
namespace {

// Default dialog window width in pixels.
constexpr float kDialogWidth = 320.0f;

// Dialog window height for Open mode in pixels.
constexpr float kOpenDialogHeight = 420.0f;

// Dialog window height for Save mode in pixels.
constexpr float kSaveDialogHeight = 460.0f;

// Width of the Back navigation button in pixels.
constexpr float kBackButtonWidth = 60.0f;

// Height of the Back navigation button in pixels.
constexpr float kBackButtonHeight = 32.0f;

// Width of action buttons (Cancel, Open, Save) in pixels.
constexpr float kActionButtonWidth = 70.0f;

// Height of action buttons in pixels.
constexpr float kActionButtonHeight = 32.0f;

// Row hover background color.
constexpr uint32 kHoverColor = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);

// Row selected background color.
constexpr uint32 kSelectedColor = SkColorSetARGB(0xFF, 0xE0, 0xE7, 0xFF);

// Default background color for the dialog container.
constexpr uint32 kContainerBackgroundColor = 0xFFF3F4F6;

// Text color for error states in path input.
constexpr uint32 kErrorTextColor = 0xFFB91C1C;

// Status label text color.
constexpr uint32 kStatusTextColor = 0xFF6B7280;

// Item label text color.
constexpr uint32 kItemTextColor = 0xFF1F2937;

struct FileDialogState {
  FileDialogOptions options;

  std::string current_path;
  std::vector<std::filesystem::directory_entry> current_items;
  std::string selected_file_path;
  std::string filename;

  std::shared_ptr<Node> files_list_container;
  std::shared_ptr<Node> path_label;
  std::shared_ptr<Node> filename_input;
  std::shared_ptr<Node> status_label;
  std::shared_ptr<Node> back_button;
  std::shared_ptr<Node> action_button;
  std::shared_ptr<Node> cancel_button;

  std::vector<std::string> normalized_filters;
  bool apply_filters = true;
  std::shared_ptr<bool> completed;

  std::weak_ptr<Node> window_node;
};

// Global collection of open dialog windows to keep them alive and close them
// correctly.
std::vector<std::shared_ptr<Node>> active_dialogs;

// Remembers the last visited directory across dialog sessions.
std::string last_visited_directory = "";

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

void NavigateTo(const std::shared_ptr<FileDialogState>& state,
                const std::string& path);
void CloseDialog(std::shared_ptr<Node> window_node);
void PerformAction(const std::shared_ptr<FileDialogState>& state);
void HandleCallback(const std::shared_ptr<FileDialogState>& state, bool success,
                    std::string_view path);

void NavigateTo(const std::shared_ptr<FileDialogState>& state,
                const std::string& path) {
  std::string target_path = path;
  if (target_path.empty()) target_path = "/";

  if (target_path.size() > 1 && target_path.back() == '/')
    target_path.pop_back();

  std::error_code ec;
  if (std::filesystem::is_symlink(target_path, ec)) {
    auto resolved = std::filesystem::read_symlink(target_path, ec);
    if (!ec && !resolved.empty()) target_path = resolved.string();
  }

  std::vector<std::filesystem::directory_entry> folders;
  std::vector<std::filesystem::directory_entry> files;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(target_path)) {
      std::string name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.') continue;

      if (entry.is_directory()) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".app") continue;
        folders.push_back(entry);
      } else {
        if (state->apply_filters && !state->normalized_filters.empty()) {
          std::string ext = GetExtension(name);
          if (std::find(state->normalized_filters.begin(),
                        state->normalized_filters.end(),
                        ext) == state->normalized_filters.end())
            continue;
        }
        files.push_back(entry);
      }
    }
  } catch (const std::exception& e) {
    std::cerr << "Error reading directory: " << target_path << ": " << e.what()
              << std::endl;
  }

  auto sort_and_insert =
      [&](std::vector<std::filesystem::directory_entry>& entries) {
        auto compare_entries = [](const std::filesystem::directory_entry& a,
                                  const std::filesystem::directory_entry& b) {
          return a.path().filename().string() < b.path().filename().string();
        };

        std::sort(entries.begin(), entries.end(), compare_entries);
        state->current_items.insert(state->current_items.end(), entries.begin(),
                                    entries.end());
      };

  state->current_items.clear();
  sort_and_insert(folders);
  sort_and_insert(files);

  state->current_path = target_path;
  state->selected_file_path = "";

  if (state->options.type == FileDialogType::Open) {
    if (state->action_button)
      state->action_button->Get<Button>()->SetButtonStyle(
          ButtonStyle::DISABLED);
  } else {
    if (state->action_button) {
      state->action_button->Get<Button>()->SetButtonStyle(
          state->filename.empty() ? ButtonStyle::DISABLED
                                  : ButtonStyle::PRIMARY);
    }
  }

  if (state->path_label) {
    auto input_box = state->path_label->Get<InputBox>();
    input_box->SetText(state->current_path);
    input_box->SetTextColor(kTextBoxTextColor);
  }

  if (state->status_label) {
    std::string status_text =
        std::to_string(state->current_items.size()) + " items";
    state->status_label->Get<Label>()->SetText(status_text);
    state->status_label->Invalidate();
  }

  if (state->back_button) {
    auto button = state->back_button->Get<Button>();
    if (state->current_path == "/") {
      button->SetButtonStyle(ButtonStyle::DISABLED);
    } else {
      button->SetButtonStyle(ButtonStyle::SECONDARY);
    }
    state->back_button->Invalidate();
  }

  if (state->files_list_container) {
    state->files_list_container->RemoveChildren();

    std::vector<std::shared_ptr<Node>> row_widgets;
    for (size_t i = 0; i < state->current_items.size(); i++) {
      const auto& entry = state->current_items[i];
      std::string name = entry.path().filename().string();
      std::string entry_path = entry.path().string();
      bool is_dir = entry.is_directory();

      bool is_symlink = entry.is_symlink(ec);
      auto icon = CreateFileIcon(is_dir, is_symlink, name);
      icon->GetLayout().SetMargin(YGEdgeRight, 8.0f);

      auto row = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetPadding(YGEdgeHorizontal, 12.0f);
            layout.SetPadding(YGEdgeVertical, 4.0f);
          },
          [](Block& block) {
            block.SetBorderRadius(6.0f);
            block.SetFillColor(0);
          },
          [state, entry_path, name, is_dir](Node& node) {
            auto* row_ptr = &node;
            node.OnMouseHover([row_ptr, state, entry_path](const Point& point) {
              if (state->selected_file_path == entry_path) return;
              auto block = row_ptr->Get<Block>();
              if (block->GetFillColor() != kHoverColor) {
                block->SetFillColor(kHoverColor);
                row_ptr->Invalidate();
              }
            });
            node.OnMouseLeave([row_ptr, state, entry_path]() {
              if (state->selected_file_path == entry_path) return;
              row_ptr->Get<Block>()->SetFillColor(0);
              row_ptr->Invalidate();
            });
            node.OnMouseButtonDown([row_ptr, state, entry_path, name, is_dir](
                                       const Point& point, MouseButton button) {
              if (button == MouseButton::Left) {
                if (is_dir) {
                  ::perception::Defer(
                      [state, entry_path]() { NavigateTo(state, entry_path); });
                } else {
                  state->selected_file_path = entry_path;
                  if (state->options.type == FileDialogType::Save)
                    state->filename = name;

                  ::perception::Defer([state, entry_path, name]() {
                    if (state->options.type == FileDialogType::Save &&
                        state->filename_input) {
                      if (auto input = state->filename_input->Get<InputBox>())
                        input->SetText(name);
                    }

                    auto children = state->files_list_container->GetChildren();
                    auto child_it = children.begin();
                    for (size_t j = 0; j < state->current_items.size() &&
                                       child_it != children.end();
                         j++, ++child_it) {
                      auto child_row = *child_it;
                      auto item_path = state->current_items[j].path().string();
                      auto block = child_row->Get<Block>();
                      if (item_path == entry_path) {
                        block->SetFillColor(kSelectedColor);
                      } else {
                        block->SetFillColor(0);
                      }
                      child_row->Invalidate();
                    }

                    if (state->action_button) {
                      state->action_button->Get<Button>()->SetButtonStyle(
                          ButtonStyle::PRIMARY);
                    }
                  });
                }
              }
            });
          },
          icon,
          Label::BasicLabel(
              name, [](Layout& layout) { layout.SetFlexGrow(1.0f); },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(kItemTextColor);
              }));

      row_widgets.push_back(row);
    }

    state->files_list_container->AddChildren(row_widgets);
    state->files_list_container->Invalidate();
  }
}

void GoBack(const std::shared_ptr<FileDialogState>& state) {
  if (state->current_path == "/") return;
  std::filesystem::path p(state->current_path);
  std::string parent = p.parent_path().string();
  if (parent.empty()) parent = "/";
  NavigateTo(state, parent);
}

void CloseDialog(std::shared_ptr<Node> window_node) {
  ::perception::Defer([window_node]() {
    auto it =
        std::find(active_dialogs.begin(), active_dialogs.end(), window_node);
    if (it != active_dialogs.end()) active_dialogs.erase(it);
  });
}

void PerformAction(const std::shared_ptr<FileDialogState>& state) {
  if (state->options.type == FileDialogType::Open) {
    if (!state->selected_file_path.empty())
      HandleCallback(state, true, state->selected_file_path);
    return;
  }

  if (state->filename_input) {
    if (auto input = state->filename_input->Get<InputBox>())
      state->filename = input->GetText();
  }

  if (state->filename.empty()) return;

  std::string final_filename = state->filename;

  if (!state->normalized_filters.empty()) {
    std::string current_ext = GetExtension(final_filename);
    bool has_matching_ext = false;
    for (const auto& filter_ext : state->normalized_filters) {
      if (current_ext == filter_ext) {
        has_matching_ext = true;
        break;
      }
    }
    if (!has_matching_ext) final_filename += "." + state->normalized_filters[0];
  }

  std::string full_path = state->current_path;
  if (full_path.empty() || full_path == "/") {
    full_path = "/" + final_filename;
  } else {
    if (full_path.back() != '/') full_path += "/";
    full_path += final_filename;
  }

  HandleCallback(state, true, full_path);
}

void HandleCallback(const std::shared_ptr<FileDialogState>& state, bool success,
                    std::string_view path) {
  if (*state->completed) return;
  *state->completed = true;

  if (success) {
    std::error_code ec;
    std::filesystem::path file_path(path);
    if (file_path.has_parent_path())
      last_visited_directory = file_path.parent_path().string();
  }

  if (state->options.on_complete) state->options.on_complete(success, path);

  if (!state->window_node.expired()) CloseDialog(state->window_node.lock());
}

}  // namespace

void ShowFileDialog(const FileDialogOptions& options) {
  auto state = std::make_shared<FileDialogState>();
  state->options = options;
  state->completed = std::make_shared<bool>(false);

  for (const auto& ext : options.extensions_to_filter) {
    std::string e = ext;
    if (!e.empty() && e[0] == '.') e = e.substr(1);
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    state->normalized_filters.push_back(e);
  }

  std::string start_dir = "/";
  std::string initial_filename = "";

  if (options.type == FileDialogType::Save &&
      !options.default_file_path.empty()) {
    std::string file_path_str(options.default_file_path);
    size_t last_slash = file_path_str.rfind('/');
    if (last_slash != std::string::npos) {
      if (options.default_directory.empty())
        start_dir = file_path_str.substr(0, last_slash);
      initial_filename = file_path_str.substr(last_slash + 1);
    } else {
      initial_filename = file_path_str;
    }
  }

  if (!options.default_directory.empty()) {
    start_dir = std::string(options.default_directory);
  } else if (start_dir == "/" && !last_visited_directory.empty()) {
    start_dir = last_visited_directory;
  }

  std::error_code ec;
  while (!start_dir.empty() && !std::filesystem::is_directory(start_dir, ec)) {
    std::filesystem::path p(start_dir);
    if (!p.has_parent_path() || p.parent_path() == p) {
      start_dir = "/";
      break;
    }
    start_dir = p.parent_path().string();
  }
  if (start_dir.empty()) start_dir = "/";

  state->filename = initial_filename;

  auto make_checkbox = [&]() -> std::shared_ptr<Node> {
    if (options.extensions_to_filter.empty()) {
      return Node::Empty(
          [](Layout& layout) { layout.SetDisplay(YGDisplayNone); });
    }
    std::string label_text = "Only show ";
    for (size_t i = 0; i < options.extensions_to_filter.size(); i++) {
      if (i > 0) label_text += ", ";
      std::string ext = options.extensions_to_filter[i];
      if (ext.empty() || ext[0] != '.') {
        label_text += "." + ext;
      } else {
        label_text += ext;
      }
    }
    label_text += " files";

    return Checkbox::BasicCheckbox(
        label_text, true,
        [state](bool checked) {
          state->apply_filters = checked;
          ::perception::Defer(
              [state]() { NavigateTo(state, state->current_path); });
        },
        [](Layout& layout) { layout.SetMargin(YGEdgeLeft, 4.0f); });
  };

  std::string_view action_button_text =
      (options.type == FileDialogType::Save) ? "Save" : "Open";
  ButtonStyle initial_action_style =
      (options.type == FileDialogType::Save && !initial_filename.empty())
          ? ButtonStyle::PRIMARY
          : ButtonStyle::DISABLED;

  auto nav_header = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetGap(8.0f);
      },
      Button::TextButton(
          "Back",
          [state]() { ::perception::Defer([state]() { GoBack(state); }); },
          [](Layout& layout) {
            layout.SetWidth(kBackButtonWidth);
            layout.SetHeight(kBackButtonHeight);
          },
          [](Button& button) { button.SetButtonStyle(ButtonStyle::DISABLED); },
          &state->back_button),
      InputBox::BasicInputBox(
          "/",
          [state](InputBox& input_box) {
            input_box.OnEnterPressed([state,
                                      &input_box](std::string_view text) {
              std::string path_str = std::string(text);
              struct stat st;
              if (stat(path_str.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
                input_box.SetTextColor(kErrorTextColor);
              } else {
                input_box.SetTextColor(kTextBoxTextColor);
                ::perception::Defer(
                    [state, path_str]() { NavigateTo(state, path_str); });
              }
            });
          },
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinWidth(0.0f);
          },
          &state->path_label));

  auto files_list = ScrollContainer::VerticalScrollContainer(
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetPadding(YGEdgeAll, 4.0f);
            layout.SetGap(2.0f);
          },
          &state->files_list_container),
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetWidthPercent(100.0f);
      });

  std::shared_ptr<Node> filename_row = nullptr;
  if (options.type == FileDialogType::Save) {
    filename_row = Container::HorizontalContainer(
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetGap(8.0f);
        },
        Label::BasicLabel("File Name:",
                          [](Label& label) { label.SetColor(kItemTextColor); }),
        InputBox::BasicInputBox(
            initial_filename,
            [state](InputBox& input_box) {
              input_box.OnTextChanged([state](std::string_view text) {
                state->filename = std::string(text);
                if (state->action_button) {
                  if (auto btn = state->action_button->Get<Button>()) {
                    btn->SetButtonStyle(state->filename.empty()
                                            ? ButtonStyle::DISABLED
                                            : ButtonStyle::PRIMARY);
                  }
                }
              });
              input_box.OnEnterPressed([state](std::string_view text) {
                state->filename = std::string(text);
                ::perception::Defer([state]() { PerformAction(state); });
              });
            },
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
              layout.SetMinWidth(0.0f);
            },
            &state->filename_input));
  }

  auto footer = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetJustifyContent(YGJustifySpaceBetween);
      },
      Label::BasicLabel(
          "0 items", [](Layout& layout) { layout.SetMargin(YGEdgeLeft, 4.0f); },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleLeft);
            label.SetColor(kStatusTextColor);
          },
          &state->status_label),
      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetGap(8.0f); },
          Button::TextButton(
              "Cancel",
              [state]() {
                ::perception::Defer(
                    [state]() { HandleCallback(state, false, ""); });
              },
              [](Layout& layout) {
                layout.SetWidth(kActionButtonWidth);
                layout.SetHeight(kActionButtonHeight);
              },
              [](Button& button) { button.SetButtonStyle(ButtonStyle::LIGHT); },
              &state->cancel_button),
          Button::TextButton(
              action_button_text,
              [state]() {
                ::perception::Defer([state]() { PerformAction(state); });
              },
              [](Layout& layout) {
                layout.SetWidth(kActionButtonWidth);
                layout.SetHeight(kActionButtonHeight);
              },
              [initial_action_style](Button& button) {
                button.SetButtonStyle(initial_action_style);
              },
              &state->action_button)));

  auto main_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetPadding(YGEdgeAll, 12.0f);
        layout.SetGap(10.0f);
      },
      [](Block& block) { block.SetFillColor(kContainerBackgroundColor); });

  main_container->AddChild(nav_header);
  main_container->AddChild(files_list);
  if (filename_row) main_container->AddChild(filename_row);
  main_container->AddChild(make_checkbox());
  main_container->AddChild(footer);

  float dialog_height = (options.type == FileDialogType::Save)
                            ? kSaveDialogHeight
                            : kOpenDialogHeight;
  std::string dialog_title =
      !options.title.empty()
          ? std::string(options.title)
          : ((options.type == FileDialogType::Save) ? "Save File"
                                                    : "Open File");

  auto window_node = UiWindow::DialogWithTitleBar(
      dialog_title, UiWindow::Parent(options.parent_window),
      [state](UiWindow& window) {
        window.OnClose([state]() {
          ::perception::Defer([state]() { HandleCallback(state, false, ""); });
        });
      },
      [dialog_height](Layout& layout) {
        layout.SetWidth(kDialogWidth);
        layout.SetHeight(dialog_height);
      },
      main_container, &state->window_node);

  active_dialogs.push_back(window_node);

  NavigateTo(state, start_dir);
}

void ShowOpenFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_open_file,
    const std::vector<std::string>& extensions_to_filter,
    std::string_view default_directory, std::string_view title,
    std::shared_ptr<Node> parent_window) {
  ShowFileDialog(FileDialogOptions{.type = FileDialogType::Open,
                                   .on_complete = std::move(on_open_file),
                                   .extensions_to_filter = extensions_to_filter,
                                   .default_directory = default_directory,
                                   .title = title,
                                   .parent_window = parent_window});
}

void ShowSaveFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_save_file,
    const std::vector<std::string>& extensions_to_filter,
    std::string_view default_file_path, std::string_view default_directory,
    std::string_view title, std::shared_ptr<Node> parent_window) {
  ShowFileDialog(FileDialogOptions{.type = FileDialogType::Save,
                                   .on_complete = std::move(on_save_file),
                                   .extensions_to_filter = extensions_to_filter,
                                   .default_file_path = default_file_path,
                                   .default_directory = default_directory,
                                   .title = title,
                                   .parent_window = parent_window});
}

}  // namespace components
}  // namespace ui
}  // namespace perception
