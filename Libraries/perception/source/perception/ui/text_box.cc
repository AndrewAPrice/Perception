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

TextBox::TextBox() : value_(""), padding_(8), is_editable_(false),
	text_alignment_(TextAlignment::MiddleLeft), realign_text_(true) {}

TextBox::~TextBox() {}

TextBox* TextBox::SetValue(std::string_view value) {
	if (value_ == value)
		return this;

	value_ = value;

    if (width_ == kFitContent)
        InvalidateSize();

    InvalidateRender();
	realign_text_ = true;
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
	realign_text_ = true;
	return this;
}

TextBox* TextBox::SetTextAlignment(TextAlignment alignment) {
	if (text_alignment_ == alignment)
		return this;

	text_alignment_ = alignment;
	realign_text_ = true;
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

	int width = GetCalculatedWidth();
	int height = GetCalculatedHeight();

	// Left line.
	DrawYLine(draw_context.x, draw_context.y,
		height, kTextBoxTopLeftOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Top line.
	DrawXLine(draw_context.x + 1, draw_context.y,
		width - 1, kTextBoxTopLeftOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Right line.
	DrawYLine(draw_context.x + width - 1,
		draw_context.y + 1,
		height - 1, kTextBoxBottomRightOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Bottom line.
	DrawXLine(draw_context.x + 1,
		draw_context.y + height - 1,
		width - 2, kTextBoxBottomRightOutlineColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	// Draw background
	FillRectangle(draw_context.x + 1,
		draw_context.y + 1,
		draw_context.x + width - 1,
		draw_context.y + height - 1,
		kTextBoxBackgroundColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);

	if (realign_text_) {
		CalculateTextAlignment(
			value_, width - 2 - padding_ * 2, height - 2 - padding_ * 2,
			text_alignment_, *GetUiFont(), text_x_, text_y_);
		realign_text_ = false;
	}

	// Draw button text.
	GetUiFont()->DrawString(
		draw_context.x + padding_ + 1 + text_x_,
		draw_context.y + padding_ + 1 + text_y_, value_,
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

void TextBox::OnNewWidth(int width) {
	realign_text_ = true;
}

void TextBox::OnNewHeight(int height) {
	realign_text_ = true;
}

}
}