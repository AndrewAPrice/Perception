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

#include "perception/ui/components/block.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::Button>;

namespace ui {
namespace components {

Button::Button()
    : idle_color_(kButtonBackgroundColor),
      hover_color_(kButtonBackgroundHoverColor),
      pushed_color_(kButtonBackgroundPushedColor),
      is_hovering_(false),
      is_pushed_(false) {}

void Button::SetNode(std::weak_ptr<Node> node) {
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

  UpdateFillColor();
}

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

uint32 Button::GetHoverColor() const { return pushed_color_; }

void Button::SetPushedColor(uint32 color) {
  if (pushed_color_ == color) return;
  pushed_color_ = color;

  if (is_pushed_) UpdateFillColor();
}

uint32 Button::GetPushedColor() const { return pushed_color_; }

void Button::OnPush(std::function<void()> on_push) {
  on_push_.push_back(on_push);
}

void Button::UpdateFillColor() {
  if (block_.expired()) return;
  auto strong_block = block_.lock();
  strong_block->SetFillColor(GetFillColor());
}

uint32 Button::GetFillColor() {
  if (is_pushed_) {
    return pushed_color_;
  }
  if (is_hovering_) {
    return hover_color_;
  }

  return idle_color_;
}

void Button::MouseHover(const Point& point) {
  if (is_hovering_) return;
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
  if (button != window::MouseButton::Left || is_pushed_) return;
  is_pushed_ = true;
  UpdateFillColor();
}

void Button::MouseButtonUp(const Point& point, window::MouseButton button) {
  if (button != window::MouseButton::Left || !is_pushed_) return;

  is_pushed_ = false;
  UpdateFillColor();
  for (auto& handler : on_push_) handler();
}

}  // namespace components
}  // namespace ui
}  // namespace perception
