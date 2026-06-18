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

#include <functional>
#include <memory>
#include <string_view>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class Checkbox : public UniqueIdentifiableType<Checkbox> {
 public:
  // Creates a checkbox with a custom label and toggle callback.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicCheckbox(
      std::string_view text, bool checked, std::function<void(bool)> on_toggle,
      Modifiers... modifiers) {
    auto marker = Node::Empty(
        [](Layout& layout) {
          layout.SetWidth(kCheckboxMarkerSize);
          layout.SetHeight(kCheckboxMarkerSize);
        },
        [](Block& block) {
          block.SetBorderRadius(kCheckboxMarkerBorderRadius);
          block.SetFillColor(kButtonTextColor);
        });

    auto indicator = Node::Empty(
        [](Layout& layout) {
          layout.SetWidth(kCheckboxSize);
          layout.SetHeight(kCheckboxSize);
          layout.SetMinWidth(kCheckboxSize);
          layout.SetMinHeight(kCheckboxSize);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetJustifyContent(YGJustifyCenter);
        },
        [](Block& block) {
          block.SetBorderColor(kButtonOutlineColor);
          block.SetBorderRadius(kCheckboxBorderRadius);
          block.SetBorderWidth(kCheckboxBorderWidth);
        });

    auto label = Node::Empty([text](Label& label_comp) {
      label_comp.SetText(text);
      label_comp.SetMaxLines(1);
      label_comp.SetColor(kCheckboxTextColor);
    });

    auto checkbox_node = Node::Empty(
        [](Node& node) {
          node.SetBlocksHitTest(true);
          node.SetCursor(window::Cursor::Poke);
        },
        [](Layout& layout) {
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetGap(kCheckboxSpacing);
        },
        indicator, label,
        [marker, indicator, label, checked, on_toggle](Checkbox& checkbox) {
          checkbox.Initialize(indicator, marker, label, checked, on_toggle);
        },
        modifiers...);

    return checkbox_node;
  }

  Checkbox();
  void SetNode(std::weak_ptr<Node> node);
  std::weak_ptr<Node> GetNode() const;

  bool IsChecked() const;
  void SetChecked(bool checked);

  void OnToggle(std::function<void(bool)> on_toggle);

 private:
  std::weak_ptr<Node> node_;
  std::shared_ptr<Node> indicator_;
  std::shared_ptr<Node> marker_;
  std::shared_ptr<Node> label_;

  bool checked_;
  bool is_hovering_;
  bool is_pushed_;
  std::vector<std::function<void(bool)>> on_toggle_handlers_;

  void Initialize(std::shared_ptr<Node> indicator, std::shared_ptr<Node> marker,
                  std::shared_ptr<Node> label, bool checked,
                  std::function<void(bool)> on_toggle);

  void UpdateVisuals();
  void Toggle();
  uint32 GetFillColor();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Checkbox>;

}  // namespace perception
