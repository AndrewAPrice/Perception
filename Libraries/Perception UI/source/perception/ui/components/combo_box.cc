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

#include "perception/ui/components/combo_box.h"

#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/pop_up.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/shapes.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::ComboBox>;

namespace ui {
namespace components {

ComboBox::ComboBox()
    : selected_index_(-1), is_hovering_(false), is_pushed_(false) {}

void ComboBox::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) {
    block_.reset();
    label_.reset();
    return;
  }
  auto strong_node = node.lock();
  block_ = strong_node->GetOrAdd<components::Block>();

  // Create the label child
  auto label_node = Label::SingleLineTruncated("");
  label_ = label_node->Get<Label>();
  strong_node->AddChild(label_node);

  // Connect events
  strong_node->OnMouseHover([this](const Point&) {
    is_hovering_ = true;
    UpdateColors();
  });
  strong_node->OnMouseLeave([this]() {
    is_hovering_ = false;
    is_pushed_ = false;
    UpdateColors();
  });
  strong_node->OnMouseButtonDown(
      [this](const Point&, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          is_pushed_ = true;
          UpdateColors();
        }
      });
  strong_node->OnMouseButtonUp(
      [this](const Point&, window::MouseButton button) {
        if (button == window::MouseButton::Left && is_pushed_) {
          is_pushed_ = false;
          UpdateColors();
          OpenDropdown();
        }
      });

  strong_node->OnDrawPostChildren(&DrawRightAlignedArrow);

  UpdateLabelText();
  UpdateColors();
}

void ComboBox::SetOptions(const std::vector<std::string>& options) {
  options_ = options;
  if (selected_index_ >= static_cast<int>(options_.size()))
    selected_index_ = options_.empty() ? -1 : 0;
  UpdateLabelText();
}

const std::vector<std::string>& ComboBox::GetOptions() const {
  return options_;
}

void ComboBox::SetSelection(int index) {
  if (selected_index_ == index) return;
  selected_index_ = index;
  UpdateLabelText();
}

int ComboBox::GetSelection() const { return selected_index_; }

void ComboBox::OnChange(std::function<void(int)> on_change) {
  on_change_.push_back(on_change);
}

void ComboBox::UpdateLabelText() {
  if (label_.expired()) return;
  auto label = label_.lock();

  if (selected_index_ >= 0 &&
      selected_index_ < static_cast<int>(options_.size())) {
    label->SetText(options_[selected_index_]);
  } else {
    label->SetText("");
  }
}

void ComboBox::UpdateColors() {
  if (block_.expired()) return;
  block_.lock()->SetFillColor(GetFillColor());

  if (!label_.expired()) label_.lock()->SetColor(kButtonTextColor);
}

void ComboBox::OpenDropdown() {
  if (node_.expired() || options_.empty()) return;
  auto strong_node = node_.lock();

  Point pos = strong_node->GetAbsolutePosition();
  float width = strong_node->GetSize().width;
  float height = strong_node->GetSize().height;

  std::vector<std::shared_ptr<Node>> items;
  for (int i = 0; i < static_cast<int>(options_.size()); ++i) {
    std::string text = options_[i];
    items.push_back(PopUpMenu::DropDownItem(text, [this, i]() {
      SetSelection(i);
      for (const auto& cb : on_change_) {
        cb(i);
      }
    }));
  }

  auto menu =
      PopUpMenu::Container([width](Layout& l) { l.SetWidth(width); }, items);

  PopUp::Show(strong_node,
              Point{pos.x, pos.y + height + (kComboBoxBorderWidth * 2.0f)},
              menu);
}

uint32 ComboBox::GetFillColor() {
  if (is_pushed_) return kButtonBackgroundPushedColor;
  if (is_hovering_) return kButtonBackgroundHoverColor;
  return kButtonBackgroundColor;
}

}  // namespace components
}  // namespace ui
}  // namespace perception
