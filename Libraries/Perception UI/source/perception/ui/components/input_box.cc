// Copyright 2026 Google LLC
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

#include "perception/ui/components/input_box.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#include "include/core/SkCanvas.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/clipboard.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/measurements.h"
#include "perception/ui/text_handling.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::InputBox>;

namespace ui {
namespace components {

InputBox::InputBox()
    : font_(nullptr),
      cursor_index_(0),
      selection_start_index_(0),
      scroll_x_(0.0f),
      shift_pressed_(false),
      ctrl_pressed_(false),
      is_hovering_(false),
      is_pushed_(false),
      text_color_(kTextBoxTextColor) {}

void InputBox::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  // Add the focusable component to our node.
  focusable_ = strong_node->GetOrAdd<Focusable>();

  strong_node->SetBlocksHitTest(true);
  strong_node->SetCursor(window::Cursor::Caret);

  strong_node->OnDraw(std::bind_front(&InputBox::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&InputBox::Measure, this));

  strong_node->OnMouseHover([this](const Point& point) {
    is_hovering_ = true;
    if (is_pushed_) MoveCursorToPoint(point, true);
    if (auto strong = node_.lock()) strong->Invalidate();
  });

  strong_node->OnMouseLeave([this]() {
    is_hovering_ = false;
    if (auto strong = node_.lock()) strong->Invalidate();
  });

  strong_node->OnMouseButtonDown(
      [this](const Point& point, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          is_pushed_ = true;
          MoveCursorToPoint(point, shift_pressed_);
          if (auto strong = node_.lock()) strong->Invalidate();
        }
      });

  strong_node->OnMouseButtonUp(
      [this](const Point&, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          is_pushed_ = false;
          if (auto strong = node_.lock()) strong->Invalidate();
        }
      });

  focusable_->OnFocus([this]() {
    if (auto strong = node_.lock()) strong->Invalidate();
  });

  focusable_->OnUnfocus([this]() {
    shift_pressed_ = false;
    ctrl_pressed_ = false;
    selection_start_index_ = cursor_index_;
    if (auto strong = node_.lock()) strong->Invalidate();
  });

  focusable_->OnKeyDown(
      [this](const window::KeyboardKeyEvent& event) { HandleKeyDown(event); });

  focusable_->OnKeyUp(
      [this](const window::KeyboardKeyEvent& event) { HandleKeyUp(event); });
}

void InputBox::SetText(std::string_view text) {
  if (text_ == text) return;
  text_ = text;
  cursor_index_ = text_.length();
  selection_start_index_ = cursor_index_;
  scroll_x_ = 0.0f;
  EnsureCursorVisible();
  if (!node_.expired()) {
    node_.lock()->Invalidate();
  }
}

std::string InputBox::GetText() const { return text_; }

bool InputBox::HasFocus() const { return focusable_ && focusable_->HasFocus(); }

void InputBox::OnTextChanged(std::function<void(std::string_view)> handler) {
  on_text_changed_handlers_.push_back(handler);
}

void InputBox::OnEnterPressed(std::function<void(std::string_view)> handler) {
  on_enter_pressed_handlers_.push_back(handler);
}

void InputBox::SetFont(SkFont* font) {
  if (font_ == font) return;
  font_ = font;
  if (!node_.expired()) {
    auto strong = node_.lock();
    strong->Remeasure();
    strong->Invalidate();
  }
}

SkFont* InputBox::GetFont() const { return font_; }

void InputBox::SetTextColor(uint32 color) {
  if (text_color_ == color) return;
  text_color_ = color;
  if (!node_.expired()) {
    node_.lock()->Invalidate();
  }
}

uint32 InputBox::GetTextColor() const { return text_color_; }

void InputBox::Draw(const DrawContext& draw_context) {
  AssignDefaultFontIfUnassigned();

  const float& x = draw_context.area.origin.x;
  const float& y = draw_context.area.origin.y;
  const float& width = draw_context.area.size.width;
  const float& height = draw_context.area.size.height;

  bool has_focus = focusable_ && focusable_->HasFocus();

  DrawTextBoxOuter(draw_context, has_focus, is_hovering_);

  // Draw Text with clipping.
  float padding = kTextBoxPadding;
  draw_context.skia_canvas->save();

  // Clip to the client area (inside border).
  float clip_border =
      has_focus ? kTextBoxOutlineFocusedWidth : kTextBoxOutlineWidth;
  draw_context.skia_canvas->clipRect(SkRect::MakeXYWH(
      x + clip_border, y + clip_border, width - clip_border * 2.0f,
      height - clip_border * 2.0f));

  SkFontMetrics font_metrics;
  font_->getMetrics(&font_metrics);
  float font_height = font_metrics.fDescent - font_metrics.fAscent;

  // Vertically center text.
  float text_y = y + (height - font_height) / 2.0f - font_metrics.fAscent;

  float text_draw_x = x + padding - scroll_x_;

  if (has_focus && selection_start_index_ != cursor_index_) {
    size_t sel_start = std::min(selection_start_index_, cursor_index_);
    size_t sel_end = std::max(selection_start_index_, cursor_index_);

    float sel_x_start = text_draw_x;
    if (sel_start > 0) {
      sel_x_start = text_draw_x + font_->measureText(text_.data(), sel_start,
                                                     SkTextEncoding::kUTF8);
    }

    float sel_x_end = text_draw_x;
    if (sel_end > 0) {
      sel_x_end = text_draw_x + font_->measureText(text_.data(), sel_end,
                                                   SkTextEncoding::kUTF8);
    }

    SkPaint sel_paint;
    sel_paint.setAntiAlias(true);
    sel_paint.setColor(kTextBoxSelectionColor);
    sel_paint.setStyle(SkPaint::kFill_Style);

    float sel_y_top = y + (height - font_height) / 2.0f;
    draw_context.skia_canvas->drawRect(
        SkRect::MakeXYWH(sel_x_start, sel_y_top, sel_x_end - sel_x_start,
                         font_height),
        sel_paint);
  }

  SkPaint text_paint;
  text_paint.setAntiAlias(true);
  text_paint.setColor(text_color_);

  draw_context.skia_canvas->drawString(SkString(text_.data(), text_.length()),
                                       text_draw_x, text_y, *font_, text_paint);

  // 4. Draw Cursor if focused
  if (has_focus && selection_start_index_ == cursor_index_) {
    float cursor_offset_x = 0.0f;
    if (cursor_index_ > 0) {
      cursor_offset_x = font_->measureText(text_.data(), cursor_index_,
                                           SkTextEncoding::kUTF8);
    }

    float cursor_x = text_draw_x + cursor_offset_x;
    SkPaint cursor_paint;
    cursor_paint.setAntiAlias(true);
    cursor_paint.setColor(kTextBoxTextColor);
    cursor_paint.setStyle(SkPaint::kFill_Style);

    float cursor_y_top = y + (height - font_height) / 2.0f;
    float cursor_y_bottom = cursor_y_top + font_height;

    draw_context.skia_canvas->drawRect(
        SkRect::MakeXYWH(cursor_x, cursor_y_top, kTextBoxCursorWidth,
                         font_height),
        cursor_paint);
  }

  draw_context.skia_canvas->restore();
}

Size InputBox::Measure(float width, YGMeasureMode width_mode, float height,
                       YGMeasureMode height_mode) {
  AssignDefaultFontIfUnassigned();
  SkFontMetrics metrics;
  font_->getMetrics(&metrics);
  float font_height = metrics.fDescent - metrics.fAscent;
  float preferred_height =
      font_height + kTextBoxPadding * 2.0f;  // 8px padding top & bottom

  return {
      .width = CalculateMeasuredLength(width_mode, width, kTextBoxDefaultWidth),
      .height = CalculateMeasuredLength(height_mode, height, preferred_height)};
}

void InputBox::MoveCursorToPoint(const Point& point, bool extend_selection) {
  AssignDefaultFontIfUnassigned();
  float padding = kTextBoxPadding;
  float click_x = point.x - padding + scroll_x_;

  cursor_index_ = FindClosestCursorIndex(text_, click_x, font_);
  if (!extend_selection) selection_start_index_ = cursor_index_;
  EnsureCursorVisible();
}

void InputBox::EnsureCursorVisible() {
  AssignDefaultFontIfUnassigned();
  float padding = kTextBoxPadding;
  float cursor_offset_x = 0.0f;
  if (cursor_index_ > 0) {
    cursor_offset_x =
        font_->measureText(text_.data(), cursor_index_, SkTextEncoding::kUTF8);
  }

  if (auto strong = node_.lock()) {
    float visible_width = strong->GetSize().width;
    if (visible_width <= 0.0f) return;  // Layout not yet calculated

    // Check left boundary
    if (cursor_offset_x - scroll_x_ < padding) {
      scroll_x_ = std::max(0.0f, cursor_offset_x - padding);
    }
    // Check right boundary
    else if (cursor_offset_x - scroll_x_ > visible_width - padding * 2.0f) {
      scroll_x_ =
          std::max(0.0f, cursor_offset_x - (visible_width - padding * 2.0f));
    }
  }
}

void InputBox::HandleKeyDown(const window::KeyboardKeyEvent& event) {
  KeyCode key = static_cast<KeyCode>(event.key);
  if (IsShiftKey(event.key)) {
    shift_pressed_ = true;
    return;
  }

  if (IsControlKey(event.key)) {
    ctrl_pressed_ = true;
    return;
  }

  if (key == KeyCode::Backspace) {
    if (selection_start_index_ != cursor_index_) {
      size_t sel_start = std::min(selection_start_index_, cursor_index_);
      size_t sel_end = std::max(selection_start_index_, cursor_index_);
      std::string new_text = text_.substr(0, sel_start) + text_.substr(sel_end);
      text_ = new_text;
      cursor_index_ = sel_start;
      selection_start_index_ = sel_start;
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_index_ > 0) {
      size_t prev_idx = GetPreviousUtf8CharIndex(text_, cursor_index_);
      std::string new_text =
          text_.substr(0, prev_idx) + text_.substr(cursor_index_);
      text_ = new_text;
      cursor_index_ = prev_idx;
      selection_start_index_ = cursor_index_;
      EnsureCursorVisible();
      NotifyTextChanged();
    }
    return;
  }

  if (key == KeyCode::Delete) {
    if (selection_start_index_ != cursor_index_) {
      size_t sel_start = std::min(selection_start_index_, cursor_index_);
      size_t sel_end = std::max(selection_start_index_, cursor_index_);
      std::string new_text = text_.substr(0, sel_start) + text_.substr(sel_end);
      text_ = new_text;
      cursor_index_ = sel_start;
      selection_start_index_ = sel_start;
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_index_ < text_.length()) {
      size_t next_len = GetNextUtf8CharLength(text_, cursor_index_);
      std::string new_text = text_.substr(0, cursor_index_) +
                             text_.substr(cursor_index_ + next_len);
      text_ = new_text;
      EnsureCursorVisible();
      NotifyTextChanged();
    }
    return;
  }

  if (key == KeyCode::LeftArrow) {
    if (shift_pressed_) {
      if (cursor_index_ > 0) {
        cursor_index_ = GetPreviousUtf8CharIndex(text_, cursor_index_);
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      }
    } else {
      if (selection_start_index_ != cursor_index_) {
        cursor_index_ = std::min(selection_start_index_, cursor_index_);
        selection_start_index_ = cursor_index_;
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      } else if (cursor_index_ > 0) {
        cursor_index_ = GetPreviousUtf8CharIndex(text_, cursor_index_);
        selection_start_index_ = cursor_index_;
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      }
    }
    return;
  }

  if (key == KeyCode::RightArrow) {
    if (shift_pressed_) {
      if (cursor_index_ < text_.length()) {
        cursor_index_ += GetNextUtf8CharLength(text_, cursor_index_);
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      }
    } else {
      if (selection_start_index_ != cursor_index_) {
        cursor_index_ = std::max(selection_start_index_, cursor_index_);
        selection_start_index_ = cursor_index_;
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      } else if (cursor_index_ < text_.length()) {
        cursor_index_ += GetNextUtf8CharLength(text_, cursor_index_);
        selection_start_index_ = cursor_index_;
        EnsureCursorVisible();
        if (auto strong = node_.lock()) strong->Invalidate();
      }
    }
    return;
  }

  if (key == KeyCode::Home) {
    if (shift_pressed_) {
      cursor_index_ = 0;
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    } else {
      cursor_index_ = 0;
      selection_start_index_ = 0;
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    }
    return;
  }

  if (key == KeyCode::End) {
    if (shift_pressed_) {
      cursor_index_ = text_.length();
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    } else {
      cursor_index_ = text_.length();
      selection_start_index_ = text_.length();
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    }
    return;
  }

  if (key == KeyCode::Enter) {
    NotifyEnterPressed();
    return;
  }

  if (ctrl_pressed_) {
    char ascii = ScancodeToAscii(event.key, false);
    if (ascii == 'c' || ascii == 'C') {
      if (selection_start_index_ != cursor_index_) {
        size_t sel_start = std::min(selection_start_index_, cursor_index_);
        size_t sel_end = std::max(selection_start_index_, cursor_index_);
        std::string selected_text =
            text_.substr(sel_start, sel_end - sel_start);
        ::perception::SetClipboard(selected_text);
      }
    } else if (ascii == 'x' || ascii == 'X') {
      if (selection_start_index_ != cursor_index_) {
        size_t sel_start = std::min(selection_start_index_, cursor_index_);
        size_t sel_end = std::max(selection_start_index_, cursor_index_);
        std::string selected_text =
            text_.substr(sel_start, sel_end - sel_start);
        ::perception::SetClipboard(selected_text);

        text_ = text_.substr(0, sel_start) + text_.substr(sel_end);
        cursor_index_ = sel_start;
        selection_start_index_ = sel_start;
        EnsureCursorVisible();
        NotifyTextChanged();
      }
    } else if (ascii == 'v' || ascii == 'V') {
      auto status_or_val = ::perception::GetClipboard();
      if (status_or_val.Ok()) {
        std::string clipboard_str = status_or_val->ToString();

        if (!clipboard_str.empty()) {
          size_t sel_start = std::min(selection_start_index_, cursor_index_);
          size_t sel_end = std::max(selection_start_index_, cursor_index_);

          text_ = text_.substr(0, sel_start) + clipboard_str +
                  text_.substr(sel_end);
          cursor_index_ = sel_start + clipboard_str.length();
          selection_start_index_ = cursor_index_;
          EnsureCursorVisible();
          NotifyTextChanged();
        }
      }
    }
    return;
  }

  char ascii = ScancodeToAscii(event.key, shift_pressed_);
  if (ascii != '\0') {
    if (selection_start_index_ != cursor_index_) {
      size_t sel_start = std::min(selection_start_index_, cursor_index_);
      size_t sel_end = std::max(selection_start_index_, cursor_index_);
      std::string new_text =
          text_.substr(0, sel_start) + ascii + text_.substr(sel_end);
      text_ = new_text;
      cursor_index_ = sel_start + 1;
      selection_start_index_ = cursor_index_;
      EnsureCursorVisible();
      NotifyTextChanged();
    } else {
      std::string new_text =
          text_.substr(0, cursor_index_) + ascii + text_.substr(cursor_index_);
      text_ = new_text;
      cursor_index_ += 1;
      selection_start_index_ = cursor_index_;
      EnsureCursorVisible();
      NotifyTextChanged();
    }
  }
}

void InputBox::HandleKeyUp(const window::KeyboardKeyEvent& event) {
  if (IsShiftKey(event.key)) {
    shift_pressed_ = false;
  } else if (IsControlKey(event.key)) {
    ctrl_pressed_ = false;
  }
}

void InputBox::NotifyTextChanged() {
  for (auto& handler : on_text_changed_handlers_) {
    handler(text_);
  }
  if (auto strong = node_.lock()) {
    strong->Invalidate();
  }
}

void InputBox::NotifyEnterPressed() {
  for (auto& handler : on_enter_pressed_handlers_) {
    handler(text_);
  }
}

void InputBox::AssignDefaultFontIfUnassigned() {
  if (font_ == nullptr) font_ = GetBook12UiFont();
}

void InputBox::DrawTextBoxOuter(const DrawContext& draw_context, bool has_focus,
                                bool is_hovering) {
  const float& x = draw_context.area.origin.x;
  const float& y = draw_context.area.origin.y;
  const float& width = draw_context.area.size.width;
  const float& height = draw_context.area.size.height;

  // 1. Draw Background
  SkPaint bg_paint;
  bg_paint.setAntiAlias(true);
  bg_paint.setColor(kTextBoxBackgroundColor);
  bg_paint.setStyle(SkPaint::kFill_Style);
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kTextBoxCornerRadius,
                                          kTextBoxCornerRadius, bg_paint);

  // 2. Draw Outline
  SkPaint outline_paint;
  outline_paint.setAntiAlias(true);
  outline_paint.setStyle(SkPaint::kStroke_Style);

  if (has_focus) {
    outline_paint.setColor(kTextBoxOutlineFocusedColor);
    outline_paint.setStrokeWidth(kTextBoxOutlineFocusedWidth);
  } else if (is_hovering) {
    outline_paint.setColor(kTextBoxOutlineHoverColor);
    outline_paint.setStrokeWidth(kTextBoxOutlineWidth);
  } else {
    outline_paint.setColor(kTextBoxOutlineColor);
    outline_paint.setStrokeWidth(kTextBoxOutlineWidth);
  }
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kTextBoxCornerRadius,
                                          kTextBoxCornerRadius, outline_paint);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
