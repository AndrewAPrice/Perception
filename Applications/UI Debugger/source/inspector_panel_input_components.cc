// Copyright 2026
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

#include "inspector_panel_input_components.h"

#include <exception>
#include <string>

#include "perception/ui/components/container.h"
#include "perception/ui/components/drop_down_box.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/font.h"
#include "perception/ui/theme.h"

namespace ui_debugger {

std::shared_ptr<::perception::ui::Node> BuildEnumDropdown(
    std::string_view label_name, const std::vector<std::string>& options,
    std::string_view current_val,
    std::function<void(std::string_view)> on_change, bool editable) {
  int selected_idx = 0;
  if (!current_val.empty()) {
    char* end_ptr = nullptr;
    std::string s(current_val);
    long v = std::strtol(s.c_str(), &end_ptr, 10);
    if (end_ptr != s.c_str()) selected_idx = static_cast<int>(v);
  }
  if (selected_idx < 0 || selected_idx >= static_cast<int>(options.size())) {
    selected_idx = 0;
  }

  std::shared_ptr<::perception::ui::Node> val_node;
  if (editable) {
    val_node = ::perception::ui::components::DropDownBox::BasicDropDownBox(
        options, selected_idx,
        [on_change](int new_idx) {
          try {
            on_change(std::to_string(new_idx));
          } catch (...) {
          }
        },
        [](::perception::ui::Layout& l) {
          l.SetWidth(140.0f);
          l.SetFlexShrink(0.0f);
        });
  } else {
    val_node = ::perception::ui::components::Label::BasicLabel(
        options[selected_idx], [](::perception::ui::components::Label& l) {
          l.SetColor(0xFF6B7280);
        });
  }

  return ::perception::ui::components::Container::HorizontalContainer(
      [](::perception::ui::Layout& l) {
        l.SetWidthPercent(100.0f);
        l.SetAlignItems(YGAlignCenter);
        l.SetJustifyContent(YGJustifySpaceBetween);
      },
      ::perception::ui::components::Label::BasicLabel(label_name), val_node);
}

std::shared_ptr<::perception::ui::Node> BuildSingleInput(
    std::string_view label_name, std::string_view current_val,
    std::function<void(std::string_view)> on_change, bool editable) {
  std::shared_ptr<::perception::ui::Node> val_node;
  if (editable) {
    val_node = ::perception::ui::components::InputBox::BasicInputBox(
        current_val,
        [](::perception::ui::Layout& l) {
          l.SetWidth(140.0f);
          l.SetFlexShrink(0.0f);
        },
        [on_change](::perception::ui::components::InputBox& ib) {
          ib.OnTextChanged([on_change](std::string_view text) {
            try {
              on_change(text);
            } catch (...) {
            }
          });
        });
  } else {
    val_node = ::perception::ui::components::Label::BasicLabel(
        current_val, [](::perception::ui::components::Label& l) {
          l.SetColor(0xFF6B7280);
        });
  }

  return ::perception::ui::components::Container::HorizontalContainer(
      [](::perception::ui::Layout& l) {
        l.SetWidthPercent(100.0f);
        l.SetAlignItems(YGAlignCenter);
        l.SetJustifyContent(YGJustifySpaceBetween);
      },
      ::perception::ui::components::Label::BasicLabel(label_name), val_node);
}

std::shared_ptr<::perception::ui::Node> BuildPairInput(
    std::string_view label_name, std::string_view val1, std::string_view val2,
    std::function<void(std::string_view)> on_change1,
    std::function<void(std::string_view)> on_change2, bool editable) {
  std::shared_ptr<::perception::ui::Node> val_node;
  if (editable) {
    auto input1 = ::perception::ui::components::InputBox::BasicInputBox(
        val1, [](::perception::ui::Layout& l) { l.SetWidth(62.0f); },
        [on_change1](::perception::ui::components::InputBox& ib) {
          ib.OnTextChanged([on_change1](std::string_view text) {
            try {
              on_change1(text);
            } catch (...) {
            }
          });
        });

    auto x_lbl = ::perception::ui::components::Label::BasicLabel(
        "x", [](::perception::ui::Layout& l) {
          l.SetMargin(YGEdgeHorizontal, 4.0f);
        });

    auto input2 = ::perception::ui::components::InputBox::BasicInputBox(
        val2, [](::perception::ui::Layout& l) { l.SetWidth(62.0f); },
        [on_change2](::perception::ui::components::InputBox& ib) {
          ib.OnTextChanged([on_change2](std::string_view text) {
            try {
              on_change2(text);
            } catch (...) {
            }
          });
        });

    val_node =
        ::perception::ui::components::Container::HorizontalContainer(
            [](::perception::ui::Layout& l) { l.SetAlignItems(YGAlignCenter); },
            input1, x_lbl, input2);
  } else {
    val_node = ::perception::ui::components::Label::BasicLabel(
        std::string(val1) + " x " + std::string(val2),
        [](::perception::ui::components::Label& l) {
          l.SetColor(0xFF6B7280);
        });
  }

  return ::perception::ui::components::Container::HorizontalContainer(
      [](::perception::ui::Layout& l) {
        l.SetWidthPercent(100.0f);
        l.SetAlignItems(YGAlignCenter);
        l.SetJustifyContent(YGJustifySpaceBetween);
      },
      ::perception::ui::components::Label::BasicLabel(label_name), val_node);
}

std::shared_ptr<::perception::ui::Node> BuildSectionHeader(
    std::string_view title) {
  return ::perception::ui::components::Container::VerticalContainer(
      [](::perception::ui::Layout& l) {
        l.SetMargin(YGEdgeTop, 8.0f);
        l.SetMargin(YGEdgeBottom, 2.0f);
      },
      ::perception::ui::components::Label::BasicLabel(
          title, [](::perception::ui::components::Label& l) {
            l.SetFont(::perception::ui::GetBold12UiFont());
            l.SetColor(0xFF111827);
          }));
}

}  // namespace ui_debugger
