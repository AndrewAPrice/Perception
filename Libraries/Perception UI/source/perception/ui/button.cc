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
#include "perception/ui/font.h"
#include "perception/ui/theme.h"
#include "perception/window/mouse_button.h"

using ::perception::window::MouseButton;

namespace perception {
namespace ui {

constexpr uint32 kUnpushedBackgroundColor = SkColorSetARGB(0, 0, 0, 0);
constexpr uint32 kHoverBackgroundColor = SkColorSetARGB(0xFF, 0xD9, 0xD9, 0xD9);
constexpr uint32 kPushedBackgroundColor =
    SkColorSetARGB(0xFF, 0xFF, 0xFF, 0xFF);
constexpr uint32 kTextColor = SkColorSetARGB(0xFF, 0, 0, 0);
constexpr float kBorderRadius = 8.0f;

std::shared_ptr<Button> Button::Create() {
  std::shared_ptr<Button> button(new Button());

  auto label = std::make_shared<Label>();
  label->SetTextAlignment(TextAlignment::MiddleCenter)
      ->SetColor(kTextColor)
      ->SetFont(GetBold12UiFont())
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

// Sets the text color of the button. Does nothing if this is a custom button.
Button* Button::SetTextColor(uint32 color) {
  if (label_) label_->SetColor(color);

  return this;
}

// Returns the text color of the button. Returns a transparency if this is a
// custom button.
uint32 Button::GetTextColor() {
  if (label_) return label_->GetColor();

  return 0;
}

// Sets the background color of the button.
Button* Button::SetUnpushedBackgroundColor(uint32 color) {
  unpushed_background_color_ = color;
  ApplyBackgroundColor();
  return this;
}

// Returns the background color of the button.
uint32 Button::GetUnpushedBackgroundColor() {
  return unpushed_background_color_;
}

// Sets the background color when hovered over.
Button* Button::SetBackgroundHoverColor(uint32 color) {
  background_hover_color_ = color;
  ApplyBackgroundColor();
  return this;
}

// Returns the background color when hovered over.
uint32 Button::GetBackgroundHoverColor(uint32 color) {
  return background_hover_color_;
}

// Sets the background color when pushed.
Button* Button::SetBackgroundPushedColor(uint32 color) {
  background_pushed_color_ = color;
  ApplyBackgroundColor();
  return this;
}

// Returns the background color when pushed.
uint32 Button::GetBackgroundPushedColor(uint32 color) {
  return background_pushed_color_;
}

void Button::OnMouseEnter() {
  if (!is_mouse_hovering_) {
    is_mouse_hovering_ = true;
    ApplyBackgroundColor();
  }
}

void Button::OnMouseLeave() {
  is_mouse_hovering_ = false;
  is_pushed_down_ = false;
  ApplyBackgroundColor();
}

void Button::OnMouseButtonDown(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (is_pushed_down_) return;

  is_pushed_down_ = true;
  ApplyBackgroundColor();
}

void Button::OnMouseButtonUp(float x, float y, MouseButton button) {
  if (button != MouseButton::Left) return;

  if (!is_pushed_down_) return;

  is_pushed_down_ = false;
  ApplyBackgroundColor();
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

Button::Button()
    : is_pushed_down_(false),
      is_mouse_hovering_(false),
      unpushed_background_color_(kUnpushedBackgroundColor),
      background_hover_color_(kHoverBackgroundColor),
      background_pushed_color_(kPushedBackgroundColor) {
  SetMinWidth(32.0f);
  SetMinHeight(32.0f);
  //SetMargin(YGEdgeAll, kMarginAroundWidgets);
  SetBorderWidth(0.0f);
  SetBorderRadius(kBorderRadius);
  ApplyBackgroundColor();
}

void Button::ApplyBackgroundColor() {
  if (is_pushed_down_) {
    SetBackgroundColor(background_pushed_color_);
  } else if (is_mouse_hovering_) {
    SetBackgroundColor(background_hover_color_);
  } else {
    SetBackgroundColor(unpushed_background_color_);
  }
}

}  // namespace ui
}  // namespace perception