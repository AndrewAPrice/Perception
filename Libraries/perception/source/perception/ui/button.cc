// Copyright 2021 Google LLC
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

#include "perception/ui/button.h"

#include <iostream>

#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {

Button::Button() : is_pushed_down_(false) {}

Button::~Button() {}

Button* Button::OnClick(std::function<void()> on_click_handler) {
  on_click_handler_ = on_click_handler;
  return this;
}

void Button::OnMouseLeave() {
  if (is_pushed_down_) {
    is_pushed_down_ = false;
    InvalidateRender();
  }
}

void Button::OnMouseButtonDown(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (is_pushed_down_) return;

  is_pushed_down_ = true;
  InvalidateRender();
}

void Button::OnMouseButtonUp(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (!is_pushed_down_) return;

  is_pushed_down_ = false;
  InvalidateRender();

  if (on_click_handler_) on_click_handler_();
}

bool Button::GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                         float& x_in_selected_widget,
                         float& y_in_selected_widget) {
  float left = GetLeft();
  float top = GetTop();
  float right = left + GetCalculatedWidth();
  float bottom = top + GetCalculatedHeight();

  if (x < left || y < top || x >= right || y >= bottom) {
    // Out of bounds.
    return false;
  }

  widget = ToSharedPtr();
  x_in_selected_widget = x - GetLeft();
  y_in_selected_widget = y - GetTop();
  return true;
}

void Button::Draw(DrawContext& draw_context) {
  uint32 top_left_color;
  uint32 bottom_right_color;
  uint32 background_color;
  int text_offset;
  if (is_pushed_down_) {
    top_left_color = kButtonDarkColor;
    bottom_right_color = kButtonBrightColor;
    background_color = kButtonPushedBackgroundColor;
    text_offset = 1;
  } else {
    top_left_color = kButtonBrightColor;
    bottom_right_color = kButtonDarkColor;
    background_color = kButtonBackgroundColor;
    text_offset = 0;
  }

  int x = (int)(GetLeft() + draw_context.offset_x);
  int y = (int)(GetTop() + draw_context.offset_y);

  int width = (int)GetCalculatedWidth();
  int height = (int)GetCalculatedHeight();

  // Left line.
  DrawYLine(x, y, height, top_left_color, draw_context.buffer,
            draw_context.buffer_width, draw_context.buffer_height);

  // Top line.
  DrawXLine(x + 1, y, width - 1, top_left_color, draw_context.buffer,
            draw_context.buffer_width, draw_context.buffer_height);

  // Right line.
  DrawYLine(x + width - 1, y + 1, height - 1, bottom_right_color,
            draw_context.buffer, draw_context.buffer_width,
            draw_context.buffer_height);

  // Bottom line.
  DrawXLine(x + 1, y + height - 1, width - 2, bottom_right_color,
            draw_context.buffer, draw_context.buffer_width,
            draw_context.buffer_height);

  // Draw background
  FillRectangle(x + 1, y + 1, x + width - 1, y + height - 1, background_color,
                draw_context.buffer, draw_context.buffer_width,
                draw_context.buffer_height);

  Widget::Draw(draw_context);
}

}  // namespace ui
}  // namespace perception