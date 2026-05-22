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

#include <memory>
#include <string_view>
#include <type_traits>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class GroupBox : public UniqueIdentifiableType<GroupBox> {
 public:
  // Creates a group box that lays out its contents, with no default Flex
  // direction.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicGroupBox(std::string_view label_text,
                                             Modifiers... modifiers) {
    auto node = Node::Empty(
        [](Block& block) {
          block.SetFillColor(kGroupBoxBackgroundColor);
          block.SetBorderWidth(kGroupBoxBorderWidth);
          block.SetBorderColor(kGroupBoxBorderColor);
          block.SetBorderRadius(kGroupBoxBorderRadius);
        },
        [](Layout& layout) {
          layout.SetPadding(YGEdgeAll, kGroupBoxPadding);
          layout.SetMargin(YGEdgeBottom, kGroupBoxPadding);
          layout.SetGap(kWidgetSpacing);
        },
        Label::BasicLabel(
            label_text,
            [](Label& label) { label.SetColor(kGroupBoxTitleColor); },
            [](Layout& layout) {
              layout.SetMargin(YGEdgeBottom, kGroupBoxTitleMarginBottom);
            }));

    node->Apply(modifiers...);
    return node;
  }

  // Creates a group box that lays out its contents vertically.
  template <typename... Modifiers>
  static std::shared_ptr<Node> VerticalGroupBox(std::string_view label_text,
                                                Modifiers... modifiers) {
    return BasicGroupBox(
        label_text,
        [](Layout& layout) { layout.SetFlexDirection(YGFlexDirectionColumn); },
        modifiers...);
  }

  // Creates a group box that lays out its contents horizontally.
  template <typename... Modifiers>
  static std::shared_ptr<Node> HorizontalGroupBox(std::string_view label_text,
                                                  Modifiers... modifiers) {
    return BasicGroupBox(
        label_text,
        [](Layout& layout) { layout.SetFlexDirection(YGFlexDirectionRow); },
        modifiers...);
  }
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::GroupBox>;

}  // namespace perception
