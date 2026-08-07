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

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "perception/file.h"
#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/file_icon.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/text_alignment.h"
#include "perception/window/mouse_button.h"

using ::perception::FormatSize;
using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::TerminateProcess;
using ::perception::ui::CreateFileIcon;
using ::perception::ui::GetBold12UiFont;
using ::perception::ui::GetBook12UiFont;
using ::perception::ui::kTextBoxTextColor;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::PopUp;
using ::perception::ui::components::PopUpMenu;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;
using ::perception::window::MouseButton;

namespace {

// Default window width.
constexpr float kWindowWidth = 340.0f;

// Default window height.
constexpr float kWindowHeight = 400.0f;

// Color of row hover background.
constexpr uint32 kRowHoverColor = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);

// Primary header text color.
constexpr uint32 kHeaderTextColor = 0xFF4B5563;

// Active sorted column header text color.
constexpr uint32 kHeaderActiveColor = 0xFF111827;

enum class SortColumn { NAME, SIZE };
enum class SortDirection { ASCENDING, DESCENDING };

std::string current_path = "/";
std::vector<std::filesystem::directory_entry> current_items;
SortColumn sort_column = SortColumn::NAME;
SortDirection sort_direction = SortDirection::ASCENDING;

std::shared_ptr<Node> files_list_container;
std::shared_ptr<Node> path_label;
std::shared_ptr<Node> status_label;
std::shared_ptr<Node> back_button;
std::shared_ptr<Node> name_header_label;
std::shared_ptr<Node> size_header_label;

// Forward declaration of NavigateTo.
void NavigateTo(const std::string& path);

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

void CreateNewFolder() {
  std::string base_name = "New Folder";
  std::string folder_path =
      current_path == "/" ? "/" + base_name : current_path + "/" + base_name;
  int index = 1;

  std::error_code ec;
  while (std::filesystem::exists(folder_path, ec)) {
    folder_path = (current_path == "/" ? "/" : current_path + "/") + base_name +
                  " (" + std::to_string(index) + ")";
    index++;
  }

  std::filesystem::create_directory(folder_path, ec);
  NavigateTo(current_path);
}

void DeleteItem(const std::string& target_path) {
  std::error_code ec;
  std::filesystem::remove_all(target_path, ec);
  NavigateTo(current_path);
}

void ShowContextMenuForItem(Node& context_node, const Point& anchor,
                            const std::string& entry_path, bool is_dir) {
  auto menu = PopUpMenu::Container(
      PopUpMenu::ContextMenuItem(
          "Open",
          [entry_path, is_dir]() {
            if (is_dir) {
              ::perception::Defer([entry_path]() { NavigateTo(entry_path); });
            } else {
              LoadApplicationRequest request;
              request.name = entry_path;
              GetService<Loader>().LaunchApplication(request, nullptr);
            }
          }),
      PopUpMenu::ContextMenuItem(
          "New Folder",
          []() { ::perception::Defer([]() { CreateNewFolder(); }); }),
      PopUpMenu::ContextMenuItem("Delete", [entry_path]() {
        ::perception::Defer([entry_path]() { DeleteItem(entry_path); });
      }));

  PopUp::Show(context_node.shared_from_this(), anchor, menu);
}

void ShowContextMenuForBackground(Node& context_node, const Point& anchor) {
  auto menu =
      PopUpMenu::Container(PopUpMenu::ContextMenuItem("New Folder", []() {
        ::perception::Defer([]() { CreateNewFolder(); });
      }));

  PopUp::Show(context_node.shared_from_this(), anchor, menu);
}

void NavigateTo(const std::string& path) {
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
  std::vector<std::filesystem::directory_entry> apps;
  std::vector<std::filesystem::directory_entry> files;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(target_path)) {
      std::string name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.') continue;

      if (entry.is_directory()) {
        if (GetExtension(name) == "app") {
          apps.push_back(entry);
        } else {
          folders.push_back(entry);
        }
      } else {
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
          if (sort_column == SortColumn::NAME) {
            std::string name_a = a.path().filename().string();
            std::string name_b = b.path().filename().string();
            std::transform(name_a.begin(), name_a.end(), name_a.begin(),
                           ::tolower);
            std::transform(name_b.begin(), name_b.end(), name_b.begin(),
                           ::tolower);
            if (sort_direction == SortDirection::ASCENDING)
              return name_a < name_b;
            return name_a > name_b;
          } else {
            std::error_code ec_a, ec_b;
            uintmax_t size_a = a.is_directory() ? 0 : a.file_size(ec_a);
            uintmax_t size_b = b.is_directory() ? 0 : b.file_size(ec_b);
            if (sort_direction == SortDirection::ASCENDING)
              return size_a < size_b;
            return size_a > size_b;
          }
        };

        std::sort(entries.begin(), entries.end(), compare_entries);
        current_items.insert(current_items.end(), entries.begin(),
                             entries.end());
      };

  current_items.clear();
  sort_and_insert(folders);
  sort_and_insert(apps);
  sort_and_insert(files);

  current_path = target_path;

  // Update path UI.
  if (path_label) {
    auto input_box = path_label->Get<InputBox>();
    input_box->SetText(current_path);
    input_box->SetTextColor(kTextBoxTextColor);
  }

  // Update status.
  if (status_label) {
    std::string status_text = std::to_string(current_items.size()) + " items";
    status_label->Get<Label>()->SetText(status_text);
    status_label->Invalidate();
  }

  // Update Back button appearance.
  if (back_button) {
    auto button = back_button->Get<Button>();
    if (current_path == "/") {
      button->SetButtonStyle(ButtonStyle::DISABLED);
    } else {
      button->SetButtonStyle(ButtonStyle::SECONDARY);
    }
    back_button->Invalidate();
  }

  // Update Column Header Labels.
  if (name_header_label) {
    std::string text = "Name";
    if (sort_column == SortColumn::NAME) {
      text += (sort_direction == SortDirection::ASCENDING) ? " ▲" : " ▼";
    }
    auto label = name_header_label->Get<Label>();
    label->SetText(text);
    label->SetColor(sort_column == SortColumn::NAME ? kHeaderActiveColor
                                                    : kHeaderTextColor);
    name_header_label->Invalidate();
  }
  if (size_header_label) {
    std::string text = "Size";
    if (sort_column == SortColumn::SIZE) {
      text += (sort_direction == SortDirection::ASCENDING) ? " ▲" : " ▼";
    }
    auto label = size_header_label->Get<Label>();
    label->SetText(text);
    label->SetColor(sort_column == SortColumn::SIZE ? kHeaderActiveColor
                                                    : kHeaderTextColor);
    size_header_label->Invalidate();
  }

  // Populate list.
  if (files_list_container) {
    files_list_container->RemoveChildren();

    std::vector<std::shared_ptr<Node>> row_widgets;
    for (size_t i = 0; i < current_items.size(); i++) {
      const auto& entry = current_items[i];
      std::string name = entry.path().filename().string();
      std::string entry_path = entry.path().string();
      bool is_dir = entry.is_directory();

      bool is_symlink = entry.is_symlink(ec);
      auto icon = CreateFileIcon(is_dir, is_symlink, name);
      icon->GetLayout().SetMargin(YGEdgeRight, 12.0f);

      std::string size_str = "";
      if (!is_dir) {
        std::error_code size_ec;
        uintmax_t file_size = entry.file_size(size_ec);
        if (!size_ec) size_str = FormatSize(file_size);
      }

      auto row = Container::HorizontalContainer(
          [](Layout& layout) {
            layout.SetWidthPercent(100.0f);
            layout.SetAlignItems(YGAlignCenter);
            layout.SetPadding(YGEdgeHorizontal, 12.0f);
            layout.SetPadding(YGEdgeVertical, 8.0f);
          },
          [](Block& block) {
            block.SetBorderRadius(6.0f);
            block.SetFillColor(0);  // Transparent
          },
          [entry_path, is_dir](Node& node) {
            auto* row_ptr = &node;
            node.OnMouseHover([row_ptr](const Point& point) {
              auto block = row_ptr->Get<Block>();
              if (block->GetFillColor() != kRowHoverColor) {
                block->SetFillColor(kRowHoverColor);
                row_ptr->Invalidate();
              }
            });
            node.OnMouseLeave([row_ptr]() {
              row_ptr->Get<Block>()->SetFillColor(0);
              row_ptr->Invalidate();
            });
            node.OnMouseButtonDown([row_ptr, entry_path, is_dir](
                                       const Point& point, MouseButton button) {
              if (button == MouseButton::Left) {
                if (is_dir) {
                  ::perception::Defer(
                      [entry_path]() { NavigateTo(entry_path); });
                } else {
                  LoadApplicationRequest request;
                  request.name = entry_path;
                  GetService<Loader>().LaunchApplication(request, nullptr);
                }
              } else if (button == MouseButton::Right) {
                ShowContextMenuForItem(*row_ptr, point, entry_path, is_dir);
              }
            });
          },
          icon,
          Label::BasicLabel(
              name,
              [](Layout& layout) {
                layout.SetFlexGrow(1.0f);
                layout.SetFlexShrink(1.0f);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(0xFF1F2937);
                label.SetFont(GetBook12UiFont());
              }),
          Label::BasicLabel(
              size_str,
              [](Layout& layout) {
                layout.SetWidth(80.0f);
                layout.SetFlexShrink(0.0f);
              },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleRight);
                label.SetColor(0xFF6B7280);
                label.SetFont(GetBook12UiFont());
              }));

      row_widgets.push_back(row);
    }

    files_list_container->AddChildren(row_widgets);
    files_list_container->Invalidate();
  }
}

void GoBack() {
  if (current_path == "/") return;
  std::filesystem::path p(current_path);
  std::string parent = p.parent_path().string();
  if (parent.empty()) parent = "/";
  NavigateTo(parent);
}

}  // namespace

int main(int argc, char* argv[]) {
  auto window = UiWindow::ResizableWindowWithTitleBar(
      "File Manager",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      [](Layout& layout) {
        layout.SetWidth(kWindowWidth);
        layout.SetHeight(kWindowHeight);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
          },
          // Header (Back button + Path input box)
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetAlignItems(YGAlignCenter);
                layout.SetGap(8.0f);
              },
              Button::TextButton(
                  "Back", []() { ::perception::Defer([]() { GoBack(); }); },
                  [](Layout& layout) {
                    layout.SetWidth(60.0f);
                    layout.SetHeight(32.0f);
                  },
                  [](Button& button) {
                    button.SetButtonStyle(ButtonStyle::SECONDARY);
                  },
                  &back_button),
              InputBox::BasicInputBox(
                  "/",
                  [](InputBox& input_box) {
                    auto* input_ptr = &input_box;
                    input_box.OnEnterPressed(
                        [input_ptr](std::string_view text) {
                          std::string path_str = std::string(text);
                          struct stat st;
                          if (stat(path_str.c_str(), &st) != 0 ||
                              !S_ISDIR(st.st_mode)) {
                            input_ptr->SetTextColor(0xFFB91C1C);
                          } else {
                            input_ptr->SetTextColor(kTextBoxTextColor);
                            ::perception::Defer(
                                [path_str]() { NavigateTo(path_str); });
                          }
                        });
                  },
                  [](Layout& layout) {
                    layout.SetFlexGrow(1.0f);
                    layout.SetFlexShrink(1.0f);
                    layout.SetMinWidth(0.0f);
                  },
                  &path_label)),
          // Column Headers
          Container::HorizontalContainer(
              [](Layout& layout) {
                layout.SetWidthPercent(100.0f);
                layout.SetPadding(YGEdgeLeft, 18.0f);
                layout.SetPadding(YGEdgeRight, 30.0f);
                layout.SetPadding(YGEdgeTop, 6.0f);
                layout.SetPadding(YGEdgeBottom, 2.0f);
              },
              Label::BasicLabel(
                  "Name ▲", [](Layout& layout) { layout.SetFlexGrow(1.0f); },
                  [](Label& label) {
                    label.SetTextAlignment(TextAlignment::MiddleLeft);
                    label.SetColor(kHeaderActiveColor);
                    label.SetFont(GetBold12UiFont());
                  },
                  [node_ptr = &name_header_label](Node& node) {
                    *node_ptr = node.shared_from_this();
                    node.OnMouseButtonDown([](const Point& point,
                                              MouseButton button) {
                      if (button == MouseButton::Left) {
                        if (sort_column == SortColumn::NAME) {
                          sort_direction =
                              (sort_direction == SortDirection::ASCENDING)
                                  ? SortDirection::DESCENDING
                                  : SortDirection::ASCENDING;
                        } else {
                          sort_column = SortColumn::NAME;
                          sort_direction = SortDirection::ASCENDING;
                        }
                        ::perception::Defer([]() { NavigateTo(current_path); });
                      }
                    });
                  }),
              Label::BasicLabel(
                  "Size", [](Layout& layout) { layout.SetWidth(80.0f); },
                  [](Label& label) {
                    label.SetTextAlignment(TextAlignment::MiddleRight);
                    label.SetColor(kHeaderTextColor);
                    label.SetFont(GetBold12UiFont());
                  },
                  [node_ptr = &size_header_label](Node& node) {
                    *node_ptr = node.shared_from_this();
                    node.OnMouseButtonDown([](const Point& point,
                                              MouseButton button) {
                      if (button == MouseButton::Left) {
                        if (sort_column == SortColumn::SIZE) {
                          sort_direction =
                              (sort_direction == SortDirection::ASCENDING)
                                  ? SortDirection::DESCENDING
                                  : SortDirection::ASCENDING;
                        } else {
                          sort_column = SortColumn::SIZE;
                          sort_direction = SortDirection::ASCENDING;
                        }
                        ::perception::Defer([]() { NavigateTo(current_path); });
                      }
                    });
                  })),
          // Files list scroll view
          ScrollContainer::VerticalScrollContainer(
              Container::VerticalContainer(
                  [](Layout& layout) {
                    layout.SetWidthPercent(100.0f);
                    layout.SetPadding(YGEdgeAll, 6.0f);
                    layout.SetGap(4.0f);
                  },
                  [node_ptr = &files_list_container](Node& node) {
                    *node_ptr = node.shared_from_this();
                    node.OnMouseButtonDown(
                        [&node](const Point& point, MouseButton button) {
                          if (button == MouseButton::Right) {
                            ShowContextMenuForBackground(node, point);
                          }
                        });
                  }),
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
          // Status label
          Label::BasicLabel(
              "0 items",
              [](Layout& layout) { layout.SetMargin(YGEdgeLeft, 4.0f); },
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleLeft);
                label.SetColor(0xFF6B7280);
                label.SetFont(GetBook12UiFont());
              },
              &status_label)));

  std::string starting_directory = "/";
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg.size() > 1 && arg.back() == '/') arg.pop_back();
    std::error_code ec;
    if (std::filesystem::is_directory(arg, ec)) starting_directory = arg;
  }

  NavigateTo(starting_directory);

  HandOverControl();
  return 0;
}
