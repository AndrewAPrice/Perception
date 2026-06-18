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
#include <string>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

namespace perception {
namespace ui {
namespace components {

class ComboBox : public UniqueIdentifiableType<ComboBox> {
 public:
  // Creates a combo box component.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicComboBox(
      const std::vector<std::string>& options, int default_selection,
      std::function<void(int)> on_change, Modifiers... modifiers) {
    return Node::Empty(
        [](Node& node) {
          node.SetBlocksHitTest(true);
          node.SetCursor(window::Cursor::Poke);
        },
        [&options, default_selection, on_change](ComboBox& cb) {
          cb.SetOptions(options);
          cb.SetSelection(default_selection);
          cb.OnChange(on_change);
        },
        [](Layout& layout) {
          layout.SetMinWidth(kComboBoxMinWidth);
          layout.SetMinHeight(kComboBoxMinHeight);
          layout.SetAlignItems(YGAlignFlexStart);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetPadding(YGEdgeLeft, kComboBoxPaddingLeft);
          layout.SetPadding(YGEdgeRight, kComboBoxPaddingRight);
        },
        [](Block& block) {
          block.SetBorderRadius(kComboBoxBorderRadius);
          block.SetBorderWidth(kComboBoxBorderWidth);
          block.SetBorderColor(kComboBoxBorderColor);
        },
        modifiers...);
  }

  ComboBox();

  void SetNode(std::weak_ptr<Node> node);

  void SetOptions(const std::vector<std::string>& options);
  const std::vector<std::string>& GetOptions() const;

  void SetSelection(int index);
  int GetSelection() const;

  void OnChange(std::function<void(int)> on_change);

 private:
  std::vector<std::string> options_;
  int selected_index_;
  std::vector<std::function<void(int)>> on_change_;

  bool is_hovering_;
  bool is_pushed_;

  std::weak_ptr<Node> node_;
  std::weak_ptr<Block> block_;
  std::weak_ptr<Label> label_;

  void UpdateLabelText();
  void UpdateColors();
  void OpenDropdown();
  uint32 GetFillColor();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::ComboBox>;

}  // namespace perception
