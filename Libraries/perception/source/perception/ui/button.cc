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

Button::Button() : label_(""), padding_(8), is_pushed_down_(false),
	text_alignment_(TextAlignment::MiddleCenter), realign_text_(true) {}

Button::~Button() {}

Button* Button::SetLabel(std::string_view label) {
    if (label_ == label)
        return this;

    label_ = label;

    if (width_ == kFitContent)
        InvalidateSize();

    InvalidateRender();
    realign_text_ = true;
    return this;
}

Button* Button::SetPadding(int padding) {
	if (padding_ == padding)
		return this;

	padding_ = padding;
	if (width_ == kFitContent || height_ == kFitContent)
		InvalidateSize();

	InvalidateRender();
    realign_text_ = true;
	return this;
}

Button* Button::SetTextAlignment(TextAlignment alignment) {
	if (text_alignment_ == alignment)
		return this;

	text_alignment_ = alignment;
	realign_text_ = true;
	return this;
}

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

void Button::OnMouseButtonDown(int x, int y,
    MouseButton button) {
	if (button != MouseButton::Left)
		return;

	if (is_pushed_down_)
		return;

	is_pushed_down_ = true;
	InvalidateRender();
}

void Button::OnMouseButtonUp(int x, int y,
    MouseButton button) {
	if (button != MouseButton::Left)
		return;

	if (!is_pushed_down_)
		return;

	is_pushed_down_ = false;
	InvalidateRender();

	if (on_click_handler_)
		on_click_handler_();
}


bool Button::GetWidgetAt(int x, int y,
    std::shared_ptr<Widget>& widget,
    int& x_in_selected_widget,
    int& y_in_selected_widget) {
	if (x < 0 || y < 0 || x >= GetCalculatedWidth() ||
		y >= GetCalculatedHeight()) {
		// Out of bounds.
		return false;
	}

	widget = ToSharedPtr();
	x_in_selected_widget = x;
	y_in_selected_widget = y;
	return true;
}


void Button::Draw(DrawContext& draw_context) {
	VerifyCalculatedSize();

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

	int width = GetCalculatedWidth();
	int height = GetCalculatedHeight();

	// Left line.
	DrawYLine(draw_context.x, draw_context.y,
		height, top_left_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Top line.
	DrawXLine(draw_context.x + 1, draw_context.y,
		width - 1, top_left_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Right line.
	DrawYLine(draw_context.x + width - 1,
		draw_context.y + 1,
		height - 1, bottom_right_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Bottom line.
	DrawXLine(draw_context.x + 1,
		draw_context.y + height - 1,
		width - 2, bottom_right_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw background
	FillRectangle(draw_context.x + 1,
		draw_context.y + 1,
		draw_context.x + width - 1,
		draw_context.y + height - 1,
		background_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	if (realign_text_) {
		CalculateTextAlignment(
			label_, width - 2 - padding_ * 2, height - 2 - padding_ * 2,
			text_alignment_, *GetUiFont(), text_x_, text_y_);
		realign_text_ = false;
	}

	// Draw button text.
	GetUiFont()->DrawString(
		draw_context.x + padding_ + 1 + text_offset + text_x_,
		draw_context.y + padding_ + 1 + text_offset + text_y_, label_,
		kButtonTextColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);
}

int Button::CalculateContentWidth() {
	return GetUiFont()->MeasureString(label_) + padding_ * 2;
}

int Button::CalculateContentHeight() {
	return GetUiFont()->GetHeight() + padding_ * 2;
}

void Button::OnNewWidth(int width) {
	realign_text_ = true;
}

void Button::OnNewHeight(int height) {
	realign_text_ = true;
}

}
}