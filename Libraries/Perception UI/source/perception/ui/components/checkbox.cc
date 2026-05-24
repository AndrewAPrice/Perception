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

#include "perception/ui/components/checkbox.h"

#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Checkbox>;

namespace ui {
namespace components {

Checkbox::Checkbox()
    : checked_(false),
      is_hovering_(false),
      is_pushed_(false) {}

void Checkbox::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  strong_node->OnMouseHover([this](const Point&) {
    if (is_hovering_) return;
    is_hovering_ = true;
    UpdateVisuals();
  });

  strong_node->OnMouseLeave([this]() {
    if (!is_hovering_ && !is_pushed_) return;
    is_hovering_ = false;
    is_pushed_ = false;
    UpdateVisuals();
  });

  strong_node->OnMouseButtonDown([this](const Point&, window::MouseButton button) {
    if (button != window::MouseButton::Left || is_pushed_) return;
    is_pushed_ = true;
    UpdateVisuals();
  });

  strong_node->OnMouseButtonUp([this](const Point&, window::MouseButton button) {
    if (button != window::MouseButton::Left || !is_pushed_) return;
    is_pushed_ = false;
    Toggle();
  });

  strong_node->SetBlocksHitTest(true);
  strong_node->SetCursor(window::Cursor::Poke);

  UpdateVisuals();
}

std::weak_ptr<Node> Checkbox::GetNode() const {
  return node_;
}

bool Checkbox::IsChecked() const {
  return checked_;
}

void Checkbox::SetChecked(bool checked) {
  if (checked_ == checked) return;
  checked_ = checked;
  UpdateVisuals();
  for (auto& handler : on_toggle_handlers_) {
    handler(checked_);
  }
}

void Checkbox::OnToggle(std::function<void(bool)> on_toggle) {
  if (on_toggle) {
    on_toggle_handlers_.push_back(on_toggle);
  }
}

void Checkbox::Initialize(std::shared_ptr<Node> indicator,
                          std::shared_ptr<Node> marker,
                          std::shared_ptr<Node> label,
                          bool checked,
                          std::function<void(bool)> on_toggle) {
  indicator_ = indicator;
  marker_ = marker;
  label_ = label;
  checked_ = checked;
  if (on_toggle) {
    on_toggle_handlers_.push_back(on_toggle);
  }
  UpdateVisuals();
}

void Checkbox::UpdateVisuals() {
  if (!indicator_) return;

  auto block = indicator_->GetOrAdd<components::Block>();
  block->SetBorderColor(kButtonOutlineColor);

  if (is_pushed_) {
    block->SetFillColor(kButtonBackgroundPushedColor);
  } else if (is_hovering_) {
    block->SetFillColor(kButtonBackgroundHoverColor);
  } else {
    block->SetFillColor(kButtonBackgroundColor);
  }

  if (marker_) {
    auto marker_block = marker_->GetOrAdd<components::Block>();
    marker_block->SetFillColor(kButtonTextColor);

    bool has_marker = false;
    for (auto& child : indicator_->GetChildren()) {
      if (child == marker_) {
        has_marker = true;
        break;
      }
    }

    if (checked_ && !has_marker) {
      indicator_->AddChild(marker_);
      indicator_->Invalidate();
    } else if (!checked_ && has_marker) {
      indicator_->RemoveChild(marker_);
      indicator_->Invalidate();
    }
  }
}

void Checkbox::Toggle() {
  SetChecked(!checked_);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
