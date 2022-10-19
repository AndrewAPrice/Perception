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

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {

std::shared_ptr<Button> Button::Create() {
  std::shared_ptr<Button> button(new Button());

  auto label = std::make_shared<Label>();
  label->SetTextAlignment(TextAlignment::MiddleCenter)
      ->SetColor(kButtonTextColor)
      ->SetFlexGrow(1.0);
  button->label_ = label;
  button->AddChild(label);

  return button;
}

std::shared_ptr<Button> Button::CreateCustom() {
  return std::shared_ptr<Button>(new Button());
}

Button::~Button() {}

Button* Button::OnClick(std::function<void()> on_click_handler) {
  on_click_handler_ = on_click_handler;
  return this;
}

Button* Button::SetLabel(std::string_view label) {
  if (label_) label_->SetLabel(label);

  return this;
}

std::string_view Button::GetLabel() {
  if (label_) return label_->GetLabel();
  return "";
}

void Button::OnMouseEnter() {
  if (!is_mouse_hovering_) {
    is_mouse_hovering_ = true;
    InvalidateRender();
  }
}

void Button::OnMouseLeave() {
  bool invalidate = is_mouse_hovering_ || is_pushed_down_;
  is_mouse_hovering_ = false;
  is_pushed_down_ = false;

  if (invalidate) InvalidateRender();
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

Button::Button() : is_pushed_down_(false), is_mouse_hovering_(false) {
  SetMinWidth(32.0f);
  SetMinHeight(32.0f);
  SetMargin(YGEdgeAll, kMarginAroundWidgets);
}

void Button::Draw(DrawContext& draw_context) {
  uint32 outline_color = kButtonOutlineColor;
  uint32 background_color;
  int text_offset;

  if (is_pushed_down_) {
    background_color = kButtonBackgroundPushedColor;
  } else if (is_mouse_hovering_) {
    background_color = kButtonBackgroundHoverColor;
  } else {
    background_color = kButtonBackgroundColor;
  }

  float x = GetLeft() + draw_context.offset_x;
  float y = GetTop() + draw_context.offset_y;

  float width = GetCalculatedWidth();
  float height = GetCalculatedHeight();

  draw_context.skia_canvas->save();

  SkPaint p;
  p.setAntiAlias(true);

  // Draw the background.
  p.setColor(background_color);
  p.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kButtonCornerRadius,
                                          kButtonCornerRadius, p);

  // Draw the outline.
  p.setColor(outline_color);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(1);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kButtonCornerRadius,
                                          kButtonCornerRadius, p);

  draw_context.skia_canvas->restore();

  // Draw the contents of the button.
  Widget::Draw(draw_context);
}

}  // namespace ui
}  // namespace perception