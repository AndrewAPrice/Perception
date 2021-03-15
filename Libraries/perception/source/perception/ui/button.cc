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

#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

Button::Button() : label_(""), padding_(8), is_pushed_down_(false) {}

Button::~Button() {}

Button* Button::SetLabel(std::string_view label) {
    if (label_ == label)
        return this;

    label_ = label;

    if (width_ == kFitContent)
        InvalidateSize();

    InvalidateRender();
    return this;
}

Button* Button::SetPadding(int padding) {
	if (padding_ == padding)
		return this;

	padding_ = padding;
	if (width_ == kFitContent || height_ == kFitContent)
		InvalidateSize();

	InvalidateRender();
	return this;
}

Button* Button::OnClick(std::function<void()> on_click_handler) {
    on_click_handler_ = on_click_handler;
    return this;
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

	// Left line.
	DrawYLine(draw_context.x, draw_context.y,
		calculated_height_, top_left_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Top line.
	DrawXLine(draw_context.x + 1, draw_context.y,
		calculated_width_ - 1, top_left_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Right line.
	DrawYLine(draw_context.x + calculated_width_ - 1,
		draw_context.y + 1,
		calculated_height_ - 1, bottom_right_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Bottom line.
	DrawXLine(draw_context.x + 1,
		draw_context.y + calculated_height_ - 1,
		calculated_width_ - 2, bottom_right_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw background
	FillRectangle(draw_context.x + 1,
		draw_context.y + 1,
		draw_context.x + calculated_width_ - 2,
		draw_context.y + calculated_height_ - 2,
		background_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw button text.
	GetUiFont()->DrawString(
		draw_context.x + padding_ + 1 + text_offset,
		draw_context.y + padding_ + 1 + text_offset, label_,
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

}
}