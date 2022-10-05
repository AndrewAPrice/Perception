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

TextBox::TextBox()
    : value_(""),
      is_editable_(false),
      text_alignment_(TextAlignment::MiddleLeft),
      realign_text_(true) {
  YGNodeSetMeasureFunc(yoga_node_, &TextBox::Measure);
}

TextBox::~TextBox() {}

TextBox* TextBox::SetValue(std::string_view value) {
  if (value_ == value) return this;

  value_ = value;

  YGNodeMarkDirty(yoga_node_);

  InvalidateRender();
  realign_text_ = true;
  return this;
}

std::string_view TextBox::GetValue() { return value_; }

TextBox* TextBox::SetTextAlignment(TextAlignment alignment) {
  if (text_alignment_ == alignment) return this;

  text_alignment_ = alignment;
  realign_text_ = true;
  return this;
}

TextBox* TextBox::SetEditable(bool editable) {
  if (is_editable_ == editable) return this;

  is_editable_ = editable;
  return this;
}

bool TextBox::IsEditable() { return is_editable_; }

TextBox* TextBox::OnChange(std::function<void()> on_change_handler) {
  on_change_handler_ = on_change_handler;
  return this;
}

void TextBox::Draw(DrawContext& draw_context) {
  uint32 text_color;
  if (is_editable_) {
    text_color = kTextBoxNonEditableTextColor;
  } else {
    text_color = kTextBoxTextColor;
  }

  int width = (int)GetCalculatedWidth();
  int height = (int)GetCalculatedHeight();
  int x = (int)(GetLeft() + draw_context.offset_x);
  int y = (int)(GetTop() + draw_context.offset_y);

  // Left line.
  DrawYLine(x, y, height, kTextBoxTopLeftOutlineColor, draw_context.buffer,
            draw_context.buffer_width, draw_context.buffer_height);

  // Top line.
  DrawXLine(x + 1, y, width - 1, kTextBoxTopLeftOutlineColor,
            draw_context.buffer, draw_context.buffer_width,
            draw_context.buffer_height);

  // Right line.
  DrawYLine(x + width - 1, y + 1, height - 1, kTextBoxBottomRightOutlineColor,
            draw_context.buffer, draw_context.buffer_width,
            draw_context.buffer_height);

  // Bottom line.
  DrawXLine(x + 1, y + height - 1, width - 2, kTextBoxBottomRightOutlineColor,
            draw_context.buffer, draw_context.buffer_width,
            draw_context.buffer_height);

  // Draw background
  FillRectangle(x + 1, y + 1, x + width - 1, y + height - 1,
                kTextBoxBackgroundColor, draw_context.buffer,
                draw_context.buffer_width, draw_context.buffer_height);

  int left_padding = (int)GetComputedPadding(YGEdgeLeft);
  int top_padding = (int)GetComputedPadding(YGEdgeTop);
  int right_padding = (int)GetComputedPadding(YGEdgeRight);
  int bottom_padding = (int)GetComputedPadding(YGEdgeBottom);

  if (realign_text_) {
    CalculateTextAlignment(value_, width - 2 - left_padding + right_padding,
                           height - 2 - top_padding + bottom_padding,
                           text_alignment_, *GetUiFont(), text_x_, text_y_);
    realign_text_ = false;
  }

  // Draw button text.
  GetUiFont()->DrawString(x + left_padding + 1 + text_x_,
                          y + top_padding + 1 + text_y_, value_, text_color,
                          draw_context.buffer, draw_context.buffer_width,
                          draw_context.buffer_height);
}

YGSize TextBox::Measure(YGNodeRef node, float width, YGMeasureMode width_mode,
                        float height, YGMeasureMode height_mode) {
  TextBox* text_box = (TextBox*)YGNodeGetContext(node);
  YGSize size;

  if (width_mode == YGMeasureModeExactly) {
    size.width = width;
  } else {
    size.width = (float)GetUiFont()->MeasureString(text_box->value_) +
                 text_box->GetComputedPadding(YGEdgeTop) +
                 text_box->GetComputedPadding(YGEdgeBottom);
    if (width_mode == YGMeasureModeAtMost) {
      size.width = std::min(width, size.width);
    }
  }
  if (height_mode == YGMeasureModeExactly) {
    size.height = height;
  } else {
    size.height = (float)GetUiFont()->GetHeight() +
                  text_box->GetComputedPadding(YGEdgeLeft) +
                  text_box->GetComputedPadding(YGEdgeRight);
    if (height_mode == YGMeasureModeAtMost) {
      size.height = std::min(height, size.height);
    }
  }

  text_box->realign_text_ = true;
  return size;
}

}  // namespace ui
}  // namespace perception