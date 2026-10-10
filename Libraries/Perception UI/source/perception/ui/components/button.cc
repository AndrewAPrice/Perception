// Copyright 2024 Google LLC
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

#include "perception/ui/components/button.h"

#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/image_button.h"
#include "perception/ui/components/image_view.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Button>;

namespace ui {
namespace components {
namespace {

// Background color for standard buttons when disabled.
constexpr uint32 kDisabledButtonBackgroundColor = 0xFFD1D5DB;

}  // namespace

Button::Button()
    : idle_color_(kButtonBackgroundColor),
      hover_color_(kButtonBackgroundHoverColor),
      pushed_color_(kButtonBackgroundPushedColor),
      label_color_(kButtonTextColor),
      style_(ButtonStyle::DEFAULT),
      is_enabled_(true),
      is_hovering_(false),
      is_pushed_(false),
      is_toggled_(false) {}

void Button::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) {
    block_.reset();
    return;
  }
  auto strong_node = node.lock();
  block_ = strong_node->GetOrAdd<components::Block>();

  strong_node->OnMouseHover(std::bind_front(&Button::MouseHover, this));
  strong_node->OnMouseLeave(std::bind_front(&Button::MouseLeave, this));
  strong_node->OnMouseButtonDown(
      std::bind_front(&Button::MouseButtonDown, this));
  strong_node->OnMouseButtonUp(std::bind_front(&Button::MouseButtonUp, this));
  strong_node->SetBlocksHitTest(true);

  UpdateCursor();
  UpdateFillColor();
  UpdateLabelColor();
}

std::weak_ptr<Node> Button::GetNode() const { return node_; }

void Button::SetIdleColor(uint32 color) {
  if (idle_color_ == color) return;
  idle_color_ = color;

  if (!is_pushed_ && !is_hovering_) UpdateFillColor();
}

uint32 Button::GetIdleColor() const { return idle_color_; }

void Button::SetHoverColor(uint32 color) {
  if (hover_color_ == color) return;
  hover_color_ = color;

  if (!is_pushed_ && !is_hovering_) UpdateFillColor();
}

uint32 Button::GetHoverColor() const { return hover_color_; }

void Button::SetPushedColor(uint32 color) {
  if (pushed_color_ == color) return;
  pushed_color_ = color;

  if (is_pushed_) UpdateFillColor();
}

uint32 Button::GetPushedColor() const { return pushed_color_; }

void Button::SetLabelColor(uint32 color) {
  if (label_color_ == color) return;
  label_color_ = color;
  UpdateLabelColor();
}

uint32 Button::GetLabelColor() const { return label_color_; }

void Button::SetToggled(bool is_toggled) {
  if (is_toggled_ == is_toggled) return;
  is_toggled_ = is_toggled;
  UpdateFillColor();
}

bool Button::IsToggled() const { return is_toggled_; }

void Button::SetEnabled(bool enabled) {
  if (is_enabled_ == enabled) return;
  is_enabled_ = enabled;
  if (!IsEnabled()) {
    is_hovering_ = false;
    is_pushed_ = false;
  }
  UpdateCursor();
  UpdateFillColor();
  UpdateLabelColor();
}

bool Button::IsEnabled() const {
  return is_enabled_ && style_ != ButtonStyle::DISABLED;
}

void Button::SetButtonStyle(ButtonStyle style) {
  style_ = style;
  if (style == ButtonStyle::DEFAULT) {
    idle_color_ = kButtonBackgroundColor;
    hover_color_ = kButtonBackgroundHoverColor;
    pushed_color_ = kButtonBackgroundPushedColor;
    label_color_ = kButtonTextColor;
  } else if (style == ButtonStyle::RED) {
    idle_color_ = 0xFFD32F2F;
    hover_color_ = 0xFFEF5350;
    pushed_color_ = 0xFFB71C1C;
    label_color_ = 0xFFFFFFFF;
  } else if (style == ButtonStyle::PRIMARY) {
    idle_color_ = 0xFF4F46E5;
    hover_color_ = 0xFF6366F1;
    pushed_color_ = 0xFF4338CA;
    label_color_ = 0xFFFFFFFF;
  } else if (style == ButtonStyle::SECONDARY) {
    idle_color_ = 0xFF4B5563;
    hover_color_ = 0xFF6B7280;
    pushed_color_ = 0xFF374151;
    label_color_ = 0xFFFFFFFF;
  } else if (style == ButtonStyle::LIGHT) {
    idle_color_ = 0xFFE5E7EB;
    hover_color_ = 0xFFD1D5DB;
    pushed_color_ = 0xFF9CA3AF;
    label_color_ = 0xFF374151;
  } else if (style == ButtonStyle::DISABLED) {
    idle_color_ = kDisabledButtonBackgroundColor;
    hover_color_ = kDisabledButtonBackgroundColor;
    pushed_color_ = kDisabledButtonBackgroundColor;
    label_color_ = kCheckboxDisabledTextColor;
    is_hovering_ = false;
    is_pushed_ = false;
  } else if (style == ButtonStyle::GHOST) {
    idle_color_ = kButtonGhostIdleColor;
    hover_color_ = kButtonGhostHoverColor;
    pushed_color_ = kButtonGhostPushedColor;
    label_color_ = kButtonTextColor;
    if (!block_.expired())
      block_.lock()->SetBorderWidth(0.0f);
  }
  UpdateCursor();
  UpdateFillColor();
  UpdateLabelColor();
}

void Button::OnPush(std::function<void()> on_push) {
  on_push_.push_back(on_push);
}

void Button::UpdateCursor() {
  if (auto strong_node = node_.lock()) {
    strong_node->SetCursor(IsEnabled() ? window::Cursor::Poke
                                       : window::Cursor::Pointer);
  }
}

void Button::UpdateFillColor() {
  if (block_.expired()) return;
  auto strong_block = block_.lock();
  strong_block->SetFillColor(GetFillColor());
}

void Button::UpdateLabelColor() {
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  bool enabled = IsEnabled();
  uint32 effective_label_color =
      enabled ? label_color_ : kCheckboxDisabledTextColor;
  auto image_button = strong_node->Get<ImageButton>();
  for (auto& child : strong_node->GetChildren()) {
    if (auto label = child->Get<components::Label>())
      label->SetColor(effective_label_color);
    if (auto image_view = child->Get<components::ImageView>()) {
      if (!enabled) {
        image_view->SetColor(kCheckboxDisabledTextColor);
      } else if (image_button && image_button->GetColor().has_value()) {
        image_view->SetColor(*image_button->GetColor());
      } else {
        image_view->ClearColor();
      }
    }
  }
}

uint32 Button::GetFillColor() {
  if (!is_enabled_) {
    if (style_ == ButtonStyle::GHOST || idle_color_ == kButtonGhostIdleColor)
      return kButtonGhostIdleColor;
    if (auto strong_node = node_.lock()) {
      if (strong_node->Get<ImageButton>()) return kButtonGhostIdleColor;
    }
    return kDisabledButtonBackgroundColor;
  }
  if (is_pushed_ || is_toggled_) return pushed_color_;
  if (is_hovering_) return hover_color_;

  return idle_color_;
}

void Button::MouseHover(const Point& point) {
  if (!IsEnabled() || is_hovering_) return;
  is_hovering_ = true;
  UpdateFillColor();
}

void Button::MouseLeave() {
  if (!is_hovering_ && !is_pushed_) return;
  is_hovering_ = false;
  is_pushed_ = false;
  UpdateFillColor();
}

void Button::MouseButtonDown(const Point& point, window::MouseButton button) {
  if (!IsEnabled() || button != window::MouseButton::Left || is_pushed_) return;
  is_pushed_ = true;
  UpdateFillColor();
}

void Button::MouseButtonUp(const Point& point, window::MouseButton button) {
  if (!IsEnabled() || button != window::MouseButton::Left || !is_pushed_)
    return;

  is_pushed_ = false;
  UpdateFillColor();
  for (auto& handler : on_push_) ::perception::DeferAfterEvents(handler);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
