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

#include "perception/ui/file_icon.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

#include "include/core/SkColor.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/text_alignment.h"

using ::perception::ui::GetBold12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Label;

namespace perception {
namespace ui {
namespace {

std::string GetExtension(std::string_view name) {
  std::string ext = std::filesystem::path(name).extension().string();
  if (!ext.empty() && ext[0] == '.') {
    ext.erase(0, 1);
  }
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

}  // namespace

std::shared_ptr<Node> CreateFileIcon(bool is_directory, bool is_symlink,
                                     std::string_view name) {
  uint32 bg_color;
  std::string letter = "";

  if (is_directory) {
    if (GetExtension(name) == "app") {
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
    std::string ext = GetExtension(name);

    if (ext == "jpg" || ext == "png" || ext == "svg" || ext == "rgba" ||
        ext == "jpeg" || ext == "ttf" || ext == "bmp") {
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
      },
      Label::BasicLabel(
          letter, [](Layout& layout) { layout.SetFlexGrow(1.0f); },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleCenter);
            label.SetColor(0xFFFFFFFF);  // White text
            label.SetFont(GetBold12UiFont());
          }));

  if (is_symlink) {
    container->AddChild(Node::Empty(
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
        },
        Label::BasicLabel(
            "↗",
            [](Layout& layout) {
              layout.SetWidth(10.0f);
              layout.SetHeight(10.0f);
            },
            [](Label& label) {
              label.SetTextAlignment(TextAlignment::MiddleCenter);
              label.SetColor(0xFF1F2937);
              label.SetFont(GetBold12UiFont());
            })));
  }

  return container;
}

}  // namespace ui
}  // namespace perception
