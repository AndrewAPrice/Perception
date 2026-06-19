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

#include "perception/ui/components/open_file_dialog.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
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
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/text_alignment.h"
#include "perception/window/mouse_button.h"

using ::perception::ui::GetBold12UiFont;
using ::perception::ui::GetBook12UiFont;
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

struct OpenFileDialogState {
  std::string current_path;
  std::vector<std::filesystem::directory_entry> current_items;
  std::string selected_file_path;

  std::shared_ptr<Node> files_list_container;
  std::shared_ptr<Node> path_label;
  std::shared_ptr<Node> status_label;
  std::shared_ptr<Node> back_button;
  std::shared_ptr<Node> open_button;
  std::shared_ptr<Node> cancel_button;

  std::vector<std::string> normalized_filters;
  bool apply_filters = true;
  std::function<void(bool succeeded, std::string_view path)> on_open_file;
  std::shared_ptr<bool> completed;

  std::weak_ptr<Node> window_node;
};

// Global collection of open dialog windows to keep them alive and close them
// correctly.
std::vector<std::shared_ptr<Node>> active_dialogs;
std::string last_opened_directory = "";

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') {
    ext.erase(0, 1);
  }
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

// Creates a file icon.
std::shared_ptr<Node> CreateFileIcon(bool is_directory, bool is_symlink,
                                     std::string_view name) {
  uint32 bg_color;
  std::string letter = "";

  if (is_directory) {
    // Regular directory: Amber/Yellow folder color.
    bg_color = SkColorSetARGB(0xFF, 0xD9, 0x77, 0x06);
    letter = "F";
  } else {
    // File
    std::string ext = GetExtension(name);

    if (ext == "jpg" || ext == "png" || ext == "svg" || ext == "rgba" ||
        ext == "jpeg" || ext == "bmp") {
      // Image file: Emerald/Green
      bg_color = SkColorSetARGB(0xFF, 0x10, 0xB9, 0x81);
      letter = "I";
    } else if (ext == "txt" || ext == "json" || ext == "md" || ext == "xml") {
      // Text file: Cyan
      bg_color = SkColorSetARGB(0xFF, 0x06, 0xB6, 0xD4);
      letter = "T";
    } else {
      // Other file: Steel Grey
      bg_color = SkColorSetARGB(0xFF, 0x6B, 0x72, 0x80);
      letter = "D";
    }
  }

  auto container = Node::Empty(
      [](Layout& layout) {
        layout.SetWidth(24.0f);
        layout.SetHeight(24.0f);
        layout.SetAlignItems(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
      },
      [bg_color](Block& block) {
        block.SetFillColor(bg_color);
        block.SetBorderRadius(6.0f);
      });

  auto letter_label = Label::BasicLabel(
      letter,
      [](Layout& layout) {
        layout.SetWidth(24.0f);
        layout.SetHeight(24.0f);
      },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(0xFFFFFFFF);  // White text
        label.SetFont(GetBold12UiFont());
      });

  container->AddChild(letter_label);

  if (is_symlink) {
    auto shortcut_container = Node::Empty(
        [](Layout& layout) {
          layout.SetPositionType(YGPositionTypeAbsolute);
          layout.SetPosition(YGEdgeBottom, -2.0f);
          layout.SetPosition(YGEdgeRight, -2.0f);
          layout.SetWidth(12.0f);
          layout.SetHeight(12.0f);
        },
        [](Block& block) {
          block.SetFillColor(0xFFFFFFFF);
          block.SetBorderRadius(4.0f);
          block.SetBorderColor(0xFF1F2937);
          block.SetBorderWidth(1.0f);
        });
    shortcut_container->AddChild(Label::BasicLabel(
        "↗",
        [](Layout& layout) {
          layout.SetWidth(10.0f);
          layout.SetHeight(10.0f);
        },
        [](Label& label) {
          label.SetTextAlignment(TextAlignment::MiddleCenter);
          label.SetColor(0xFF1F2937);
          label.SetFont(GetBold12UiFont());
        }));
    container->AddChild(shortcut_container);
  }

  return container;
}

// Forward declarations of NavigateTo and CloseDialog.
void NavigateTo(const std::shared_ptr<OpenFileDialogState>& state,
                const std::string& path);
void CloseDialog(std::shared_ptr<Node> window_node);

void NavigateTo(const std::shared_ptr<OpenFileDialogState>& state,
                const std::string& path) {
  std::string target_path = path;
  if (target_path.empty()) target_path = "/";

  if (target_path.size() > 1 && target_path.back() == '/')
    target_path.pop_back();

  std::error_code ec;
  if (std::filesystem::is_symlink(target_path, ec)) {
    auto resolved = std::filesystem::read_symlink(target_path, ec);
    if (!ec && !resolved.empty()) {
      target_path = resolved.string();
    }
  }

  std::vector<std::filesystem::directory_entry> folders;
  std::vector<std::filesystem::directory_entry> files;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(target_path)) {
      std::string name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.') continue;

      if (entry.is_directory()) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".app") {
          // Skip app bundles in file selection dialogs
          continue;
        }
        folders.push_back(entry);
      } else {
        if (state->apply_filters && !state->normalized_filters.empty()) {
          std::string ext = GetExtension(name);
          if (std::find(state->normalized_filters.begin(),
                        state->normalized_filters.end(),
                        ext) == state->normalized_filters.end()) {
            continue;
          }
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
  state->selected_file_path = "";  // Clear selection on navigation

  // Update disabled appearance of Open button.
  if (state->open_button) {
    state->open_button->Get<Button>()->SetButtonStyle(ButtonStyle::DISABLED);
  }

  // Update path UI.
  if (state->path_label) {
    auto input_box = state->path_label->Get<InputBox>();
    input_box->SetText(state->current_path);
    input_box->SetTextColor(kTextBoxTextColor);
  }

  // Update status.
  if (state->status_label) {
    std::string status_text =
        std::to_string(state->current_items.size()) + " items";
    state->status_label->Get<Label>()->SetText(status_text);
    state->status_label->Invalidate();
  }

  // Update Back button appearance
  if (state->back_button) {
    auto button = state->back_button->Get<Button>();
    if (state->current_path == "/") {
      button->SetButtonStyle(ButtonStyle::DISABLED);
    } else {
      button->SetButtonStyle(ButtonStyle::SECONDARY);
    }
    state->back_button->Invalidate();
  }

  // Populate list.
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
            block.SetFillColor(0);  // Transparent
          },
          [state, entry_path, is_dir](Node& node) {
            auto* row_ptr = &node;
            node.OnMouseHover([row_ptr, state, entry_path](const Point& point) {
              uint32 hover_color = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);
              if (state->selected_file_path == entry_path) {
                return;
              }
              auto block = row_ptr->Get<Block>();
              if (block->GetFillColor() != hover_color) {
                block->SetFillColor(hover_color);
                row_ptr->Invalidate();
              }
            });
            node.OnMouseLeave([row_ptr, state, entry_path]() {
              if (state->selected_file_path == entry_path) {
                return;
              }
              row_ptr->Get<Block>()->SetFillColor(0);
              row_ptr->Invalidate();
            });
            node.OnMouseButtonDown([row_ptr, state, entry_path, is_dir](
                                       const Point& point, MouseButton button) {
              if (button == MouseButton::Left) {
                if (is_dir) {
                  ::perception::Defer(
                      [state, entry_path]() { NavigateTo(state, entry_path); });
                } else {
                  // Select file!
                  state->selected_file_path = entry_path;

                  ::perception::Defer([state, entry_path]() {
                    auto children = state->files_list_container->GetChildren();
                    auto child_it = children.begin();
                    for (size_t j = 0; j < state->current_items.size() &&
                                       child_it != children.end();
                         j++, ++child_it) {
                      auto child_row = *child_it;
                      auto item_path = state->current_items[j].path().string();
                      auto block = child_row->Get<Block>();
                      if (item_path == entry_path) {
                        // Selected color: #E0E7FF (Light Indigo)
                        block->SetFillColor(
                            SkColorSetARGB(0xFF, 0xE0, 0xE7, 0xFF));
                      } else {
                        block->SetFillColor(0);
                      }
                      child_row->Invalidate();
                    }

                    // Enable Open button.
                    if (state->open_button) {
                      state->open_button->Get<Button>()->SetButtonStyle(
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
                label.SetColor(0xFF1F2937);
                label.SetFont(GetBook12UiFont());
              }));

      row_widgets.push_back(row);
    }

    state->files_list_container->AddChildren(row_widgets);
    state->files_list_container->Invalidate();
  }
}

void GoBack(const std::shared_ptr<OpenFileDialogState>& state) {
  if (state->current_path == "/") {
    return;
  }
  std::filesystem::path p(state->current_path);
  std::string parent = p.parent_path().string();
  if (parent.empty()) {
    parent = "/";
  }
  NavigateTo(state, parent);
}

void CloseDialog(std::shared_ptr<Node> window_node) {
  ::perception::Defer([window_node]() {
    auto it =
        std::find(active_dialogs.begin(), active_dialogs.end(), window_node);
    if (it != active_dialogs.end()) {
      active_dialogs.erase(it);
    }
  });
}

void HandleCallback(const std::shared_ptr<OpenFileDialogState>& state,
                    bool success, std::string_view path) {
  if (*state->completed) return;
  *state->completed = true;

  if (success) {
    std::error_code ec;
    std::filesystem::path file_path(path);
    if (std::filesystem::exists(file_path, ec)) {
      last_opened_directory = file_path.parent_path().string();
    }
  }

  state->on_open_file(success, path);

  if (!state->window_node.expired()) CloseDialog(state->window_node.lock());
}

}  // namespace

void ShowOpenFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_open_file,
    const std::vector<std::string>& extensions_to_filter,
    std::string_view default_directory, std::string_view title) {
  auto state = std::make_shared<OpenFileDialogState>();
  state->on_open_file = on_open_file;
  state->completed = std::make_shared<bool>(false);

  // Normalize filter extensions (strip optional leading dots, convert to
  // lowercase)
  for (const auto& ext : extensions_to_filter) {
    std::string e = ext;
    if (!e.empty() && e[0] == '.') {
      e = e.substr(1);
    }
    std::transform(e.begin(), e.end(), e.begin(), ::tolower);
    state->normalized_filters.push_back(e);
  }

  // Determine and walk up starting directory to find one that exists
  std::string start_dir = "/";
  if (!default_directory.empty()) {
    start_dir = std::string(default_directory);
  } else if (!last_opened_directory.empty()) {
    start_dir = last_opened_directory;
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
  if (start_dir.empty()) {
    start_dir = "/";
  }

  // Construct dialog UI
  auto make_checkbox = [&]() -> std::shared_ptr<Node> {
    if (extensions_to_filter.empty()) {
      return Node::Empty(
          [](Layout& layout) { layout.SetDisplay(YGDisplayNone); });
    }
    std::string label_text = "Only show ";
    for (size_t i = 0; i < extensions_to_filter.size(); i++) {
      if (i > 0) {
        label_text += ", ";
      }
      std::string ext = extensions_to_filter[i];
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

  auto main_container = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMinHeight(0.0f);
        layout.SetPadding(YGEdgeAll, 12.0f);
        layout.SetGap(12.0f);
      },
      [](Block& block) { block.SetFillColor(0xFFF3F4F6); },
      // Header (Back button + Path input box)
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetGap(8.0f);
          },
          Button::TextButton(
              "Back",
              [state]() { ::perception::Defer([state]() { GoBack(state); }); },
              [](Layout& layout) {
                layout.SetWidth(60.0f);
                layout.SetHeight(32.0f);
              },
              [](Button& button) {
                button.SetButtonStyle(ButtonStyle::DISABLED);
              },
              &state->back_button),
          InputBox::BasicInputBox(
              "/",
              [state](InputBox& input_box) {
                input_box.OnEnterPressed(
                    [state, &input_box](std::string_view text) {
                      std::string path_str = std::string(text);
                      struct stat st;
                      if (stat(path_str.c_str(), &st) != 0 ||
                          !S_ISDIR(st.st_mode)) {
                        input_box.SetTextColor(0xFFB91C1C);
                      } else {
                        input_box.SetTextColor(kTextBoxTextColor);
                        ::perception::Defer([state, path_str]() {
                          NavigateTo(state, path_str);
                        });
                      }
                    });
              },
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
                layout.SetMinWidth(0.0f);
              },
              &state->path_label)),
      // Files list scroll view
      ScrollContainer::VerticalScrollContainer(
          Container::VerticalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetPadding(YGEdgeAll, 4.0f);
                layout.SetGap(2.0f);
              },
              &state->files_list_container),
          [](Block& block) {
            block.SetFillColor(0xFFFFFFFF);
            block.SetBorderColor(0xFFD1D5DB);
            block.SetBorderWidth(1.0f);
            block.SetBorderRadius(8.0f);
          },
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetMinHeight(0.0f);
            layout.SetWidthPercent(100.0f);
          }),
      // Optional filter checkbox
      make_checkbox(),
      // Footer (Status + Action buttons)
      Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetJustifyContent(YGJustifySpaceBetween);
          },
          Label::BasicLabel(
              "0 items",
              [](Layout& layout) { layout.SetMargin(YGEdgeLeft, 4.0f); },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(0xFF6B7280);
                label.SetFont(GetBook12UiFont());
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
                    layout.SetWidth(70.0f);
                    layout.SetHeight(32.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::LIGHT);
                  },
                  &state->cancel_button),
              Button::TextButton(
                  "Open",
                  [state]() {
                    ::perception::Defer([state]() {
                      if (!state->selected_file_path.empty()) {
                        HandleCallback(state, true, state->selected_file_path);
                      }
                    });
                  },
                  [](Layout& layout) {
                    layout.SetWidth(70.0f);
                    layout.SetHeight(32.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::DISABLED);
                  },
                  &state->open_button))));

  auto window_node = UiWindow::DialogWithTitleBar(
      title,
      [state](UiWindow& window) {
        window.OnClose([state]() {
          ::perception::Defer([state]() { HandleCallback(state, false, ""); });
        });
      },
      [](Layout& layout) {
        layout.SetWidth(320.0f);
        layout.SetHeight(420.0f);
      },
      main_container, &state->window_node);

  active_dialogs.push_back(window_node);

  NavigateTo(state, start_dir);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
