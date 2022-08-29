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

Label::Label() : label_(""),
	text_alignment_(TextAlignment::TopLeft), realign_text_(true) {
	YGNodeSetMeasureFunc(yoga_node_, &Label::Measure);
	}

Label::~Label() {}

Label* Label::SetLabel(std::string_view label) {
	if (label_ == label)
		return this;

	label_ = label;

	YGNodeMarkDirty(yoga_node_);
    InvalidateRender();
	realign_text_ = true;
    return this;
}

std::string_view Label::GetLabel() {
	return label_;
}

Label* Label::SetTextAlignment(TextAlignment alignment) {
	if (text_alignment_ == alignment)
		return this;

	text_alignment_ = alignment;
	realign_text_ = true;
	return this;
}


void Label::Draw(DrawContext& draw_context) {
	int width = (int)GetCalculatedWidth();
	int height = (int)GetCalculatedHeight();
	int x = (int)(GetLeft() + draw_context.offset_x);
	int y = (int)(GetTop() + draw_context.offset_y);

	if (realign_text_) {
		CalculateTextAlignment(
			label_, width - 2, height - 2,
			text_alignment_, *GetUiFont(), text_x_, text_y_);
		realign_text_ = false;
	}
	std::cout << "my size: "  << GetCalculatedWidth() << "x" << GetCalculatedHeight() <<
	" at: " << x << "x" << y << " buffer: " 
	<<  draw_context.buffer_width << "x" << draw_context.buffer_height << std::endl;

	// Draw button text.
	GetUiFont()->DrawString(
		x + 1 + text_x_,
		y+ 1 + text_y_, label_,
		kLabelTextColor,
		draw_context.buffer, draw_context.buffer_width,
		draw_context.buffer_height);
}

YGSize Label::Measure(YGNodeRef node, float width, YGMeasureMode width_mode,
	float height, YGMeasureMode height_mode) {
	Label* label = (Label*) YGNodeGetContext(node);
	YGSize size;

	if (width_mode == YGMeasureModeExactly) {

	std::cout << "Width mode is YGMeasureModeExactly " << size.width << std::endl;
		size.width = width;
	} else {
		size.width = (float)GetUiFont()->MeasureString(label->label_) +
			label->GetComputedPadding(YGEdgeLeft) +
			label->GetComputedPadding(YGEdgeRight);
		 if (width_mode == YGMeasureModeAtMost) {

	std::cout << "Width mode is YGMeasureModeAtMost (" << width << "," << size.width << ")" << std::endl;
			size.width = std::min(width, size.width);
		 } else {

	std::cout << "Width mode is Undefined (" << width << ")" << std::endl;
		 }
	}
	if (height_mode == YGMeasureModeExactly) {
		size.height = height;
	std::cout << "Height mode is YGMeasureModeExactly " << size.height << std::endl;
	} else {
		size.height = (float)GetUiFont()->GetHeight() +
			label->GetComputedPadding(YGEdgeTop) +
			label->GetComputedPadding(YGEdgeBottom);
		if (height_mode == YGMeasureModeAtMost) {
			size.height = std::min(height, size.height);
			std::cout << "Height mode is YGMeasureModeAtMost (" << height << "," << size.height << ")" << std::endl;
		} else {

			std::cout << "Height mode is undefined (" << height << ")"<<std::endl;
		}
	}

	label->realign_text_ = true;
	return size;
}

}
}