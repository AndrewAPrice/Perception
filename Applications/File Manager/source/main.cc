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
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
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

using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::TerminateProcess;
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
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;
using ButtonStyle = ::perception::ui::components::Button::ButtonStyle;
using ::perception::window::MouseButton;

namespace {

std::string current_path = "/";
std::vector<std::filesystem::directory_entry> current_items;

std::shared_ptr<Node> files_list_container;
std::shared_ptr<Node> path_label;
std::shared_ptr<Node> status_label;
std::shared_ptr<Node> back_button;

// Forward declaration of NavigateTo.
void NavigateTo(const std::string& path);

// Creates an file icon.
std::shared_ptr<Node> CreateFileIcon(bool is_directory, std::string_view name) {
  uint32 bg_color;
  std::string letter = "";

  if (is_directory) {
    if (name.size() > 4 && name.substr(name.size() - 4) == ".app") {
      // Application: vibrant Indigo.
      bg_color = SkColorSetARGB(0xFF, 0x63, 0x66, 0xF1);
      letter = "A";
    } else {
      // Regular directory: Amber/Yellow folder color.
      bg_color = SkColorSetARGB(0xFF, 0xD9, 0x77, 0x06);
      letter = "F";
    }
  } else {
    // File
    // Detect file extension
    std::string name_str = std::string(name);
    std::string ext = "";
    auto dot_idx = name_str.find_last_of('.');
    if (dot_idx != std::string::npos) {
      ext = name_str.substr(dot_idx + 1);
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    }

    if (ext == "jpg" || ext == "png" || ext == "svg" || ext == "rgba" ||
        ext == "jpeg") {
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
      letter, [](Layout& layout) { layout.SetFlexGrow(1.0f); },
      [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        label.SetColor(0xFFFFFFFF);  // White text
        label.SetFont(GetBold12UiFont());
      });

  container->AddChild(letter_label);
  return container;
}

void NavigateTo(const std::string& path) {
  std::string target_path = path;
  if (target_path.empty()) target_path = "/";

  if (target_path.size() > 1 && target_path.back() == '/')
    target_path.pop_back();

  std::vector<std::filesystem::directory_entry> folders;
  std::vector<std::filesystem::directory_entry> apps;
  std::vector<std::filesystem::directory_entry> files;

  try {
    for (const auto& entry : std::filesystem::directory_iterator(target_path)) {
      std::string name = entry.path().filename().string();
      if (!name.empty() && name[0] == '.') continue;

      if (entry.is_directory()) {
        if (name.size() > 4 && name.substr(name.size() - 4) == ".app") {
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
          return a.path().filename().string() < b.path().filename().string();
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

  // Update Back button appearance
  if (back_button) {
    auto button = back_button->Get<Button>();
    if (current_path == "/") {
      button->SetButtonStyle(ButtonStyle::DISABLED);
    } else {
      button->SetButtonStyle(ButtonStyle::SECONDARY);
    }
    back_button->Invalidate();
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

      auto icon = CreateFileIcon(is_dir, name);
      icon->GetLayout().SetMargin(YGEdgeRight, 12.0f);

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
              uint32 hover_color = SkColorSetARGB(0xFF, 0xE5, 0xE7, 0xEB);
              auto block = row_ptr->Get<Block>();
              if (block->GetFillColor() != hover_color) {
                block->SetFillColor(hover_color);
                row_ptr->Invalidate();
              }
            });
            node.OnMouseLeave([row_ptr]() {
              row_ptr->Get<Block>()->SetFillColor(0);
              row_ptr->Invalidate();
            });
            node.OnMouseButtonDown(
                [entry_path, is_dir](const Point& point, MouseButton button) {
                  if (button == MouseButton::Left) {
                    if (is_dir) {
                      ::perception::Defer(
                          [entry_path]() { NavigateTo(entry_path); });
                    } else {
                      LoadApplicationRequest request;
                      request.name = entry_path;
                      GetService<Loader>().LaunchApplication(request, nullptr);
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

    files_list_container->AddChildren(row_widgets);
    files_list_container->Invalidate();
  }
}

void GoBack() {
  if (current_path == "/") {
    return;
  }
  std::filesystem::path p(current_path);
  std::string parent = p.parent_path().string();
  if (parent.empty()) {
    parent = "/";
  }
  NavigateTo(parent);
}

}  // namespace

int main(int argc, char* argv[]) {
  auto window = UiWindow::ResizableWindowWithTitleBar(
      "File Manager",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      [](Layout& layout) {
        layout.SetWidth(300.0f);
        layout.SetHeight(400.0f);
      },
      Container::VerticalContainer(
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
          // Files list scroll view
          ScrollContainer::VerticalScrollContainer(
              Container::VerticalContainer(
                  [](Layout& layout) {
                    layout.SetWidthPercent(100.0f);
                    layout.SetPadding(YGEdgeAll, 6.0f);
                    layout.SetGap(4.0f);
                  },
                  &files_list_container),
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
    if (arg.size() > 1 && arg.back() == '/') {
      arg.pop_back();
    }
    std::error_code ec;
    if (std::filesystem::is_directory(arg, ec)) {
      starting_directory = arg;
    }
  }

  NavigateTo(starting_directory);

  HandOverControl();
  return 0;
}
