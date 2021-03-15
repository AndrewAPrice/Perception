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

#include "perception/ui/text_box.h"

#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/button.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

TextBox::TextBox() : value_(""), padding_(8), is_editable_(false) {}

TextBox::~TextBox() {}

TextBox* TextBox::SetValue(std::string_view value) {
	if (value_ == value)
		return this;

    if (width_ == kFitContent)
        InvalidateSize();

    InvalidateRender();
    return this;
}

std::string_view TextBox::GetValue() {
	return value_;
}

TextBox* TextBox::SetPadding(int padding) {
	if (padding_ == padding)
		return this;

	padding_ = padding;
	if (width_ == kFitContent || height_ == kFitContent)
		InvalidateSize();

	InvalidateRender();
	return this;
}

TextBox* TextBox::SetEditable(bool editable) {
	if (is_editable_ == editable)
		return this;

	is_editable_ = editable;
	return this;
}

bool TextBox::IsEditable() {
	return is_editable_;
}

TextBox* TextBox::OnChange(std::function<void()> on_change_handler) {
	on_change_handler_ = on_change_handler;
	return this;
}

void TextBox::Draw(DrawContext& draw_context) {
	VerifyCalculatedSize();
	uint32 text_color;
	if (is_editable_) {
		text_color = kTextBoxNonEditableTextColor;
	} else {
		text_color = kTextBoxTextColor;
	}

	// Left line.
	DrawYLine(draw_context.x, draw_context.y,
		calculated_height_, kTextBoxOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Top line.
	DrawXLine(draw_context.x + 1, draw_context.y,
		calculated_width_ - 1, kTextBoxOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Right line.
	DrawYLine(draw_context.x + calculated_width_ - 1,
		draw_context.y + 1,
		calculated_height_ - 1, kTextBoxOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Bottom line.
	DrawXLine(draw_context.x + 1,
		draw_context.y + calculated_height_ - 1,
		calculated_width_ - 2, kTextBoxOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw background
	FillRectangle(draw_context.x + 1,
		draw_context.y + 1,
		draw_context.x + calculated_width_ - 2,
		draw_context.y + calculated_height_ - 2,
		kTextBoxBackgroundColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw button text.
	GetUiFont()->DrawString(
		draw_context.x + padding_ + 1,
		draw_context.y + padding_ + 1, value_,
		text_color,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);
}

int TextBox::CalculateContentWidth() {
	return GetUiFont()->MeasureString(value_) + padding_ * 2;
}

int TextBox::CalculateContentHeight() {
	return GetUiFont()->GetHeight() + padding_ * 2;
}

}
}