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

#include "perception/ui/label.h"

#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/button.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

Label::Label() : label_(""), padding_(0),
	text_alignment_(TextAlignment::TopLeft), realign_text_(true) {}

Label::~Label() {}

Label* Label::SetLabel(std::string_view label) {
	if (label_ == label)
		return this;

	label_ = label;

    if (width_ == kFitContent)
        InvalidateSize();

    InvalidateRender();
	realign_text_ = true;
    return this;
}

std::string_view Label::GetLabel() {
	return label_;
}

Label* Label::SetPadding(int padding) {
	if (padding_ == padding)
		return this;

	padding_ = padding;
	if (width_ == kFitContent || height_ == kFitContent)
		InvalidateSize();

	InvalidateRender();
	realign_text_ = true;
	return this;
}

Label* Label::SetTextAlignment(TextAlignment alignment) {
	if (text_alignment_ == alignment)
		return this;

	text_alignment_ = alignment;
	realign_text_ = true;
	return this;
}


void Label::Draw(DrawContext& draw_context) {
	VerifyCalculatedSize();

	int width = GetCalculatedWidth();
	int height = GetCalculatedHeight();

	if (realign_text_) {
		CalculateTextAlignment(
			label_, width - 2 - padding_ * 2, height - 2 - padding_ * 2,
			text_alignment_, *GetUiFont(), text_x_, text_y_);
		realign_text_ = false;
	}

	// Draw button text.
	GetUiFont()->DrawString(
		draw_context.x + padding_ + 1 + text_x_,
		draw_context.y + padding_ + 1 + text_y_, label_,
		kLabelTextColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);
}

int Label::CalculateContentWidth() {
	return GetUiFont()->MeasureString(label_) + padding_ * 2;
}

int Label::CalculateContentHeight() {
	return GetUiFont()->GetHeight() + padding_ * 2;
}

void Label::OnNewWidth(int width) {
	realign_text_ = true;
}

void Label::OnNewHeight(int height) {
	realign_text_ = true;
}

}
}