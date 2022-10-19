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

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/draw.h"
#include "perception/ui/button.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

std::shared_ptr<TextBox> TextBox::Create() {
  std::shared_ptr<TextBox> text_box(new TextBox());
  auto label = std::make_shared<Label>();
  text_box->label_ = label;
  label->SetColor(kTextBoxTextColor)->SetFlexGrow(1.0);
  text_box->AddChild(label);
  return text_box;
}

TextBox::~TextBox() {}

TextBox* TextBox::SetValue(std::string_view value) {
  label_->SetLabel(value);
  if (on_change_handler_) on_change_handler_();
  return this;
}

std::string_view TextBox::GetValue() { return label_->GetLabel(); }

TextBox* TextBox::SetTextAlignment(TextAlignment alignment) {
  label_->SetTextAlignment(alignment);
  return this;
}

TextBox* TextBox::SetEditable(bool editable) {
  if (is_editable_ == editable) return this;

  is_editable_ = editable;

  if (is_editable_) {
    label_->SetColor(kTextBoxNonEditableTextColor);
  } else {
    label_->SetColor(kTextBoxTextColor);
  }

  return this;
}

bool TextBox::IsEditable() { return is_editable_; }

TextBox* TextBox::OnChange(std::function<void()> on_change_handler) {
  on_change_handler_ = on_change_handler;
  return this;
}

TextBox::TextBox() : is_editable_(false), Widget() {
  SetMinWidth(32.0f);
  SetMargin(YGEdgeAll, kMarginAroundWidgets);
}

void TextBox::Draw(DrawContext& draw_context) {
  float x = GetLeft() + draw_context.offset_x;
  float y = GetTop() + draw_context.offset_y;

  float width = GetCalculatedWidth();
  float height = GetCalculatedHeight();

  draw_context.skia_canvas->save();

  SkPaint p;
  p.setAntiAlias(true);

  // Draw the background.
  p.setColor(kTextBoxBackgroundColor);
  p.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kTextBoxCornerRadius,
                                          kTextBoxCornerRadius, p);

  // Draw the outline.
  p.setColor(kTextBoxOutlineColor);
  p.setStyle(SkPaint::kStroke_Style);
  p.setStrokeWidth(1);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kTextBoxCornerRadius,
                                          kTextBoxCornerRadius, p);

  draw_context.skia_canvas->restore();

  // Draw the contents of the text box.
  Widget::Draw(draw_context);
}

}  // namespace ui
}  // namespace perception