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
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/measurements.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::InputBox>;

namespace ui {
namespace components {
namespace {

size_t GetNextUtf8CharLength(std::string_view s, size_t index) {
  if (index >= s.length()) return 0;
  unsigned char c = s[index];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

size_t GetPreviousUtf8CharIndex(std::string_view s, size_t index) {
  if (index == 0 || index > s.length()) return 0;
  size_t prev = index - 1;
  while (prev > 0 && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) {
    prev--;
  }
  return prev;
}

}  // namespace

InputBox::InputBox()
    : font_(nullptr),
      cursor_index_(0),
      scroll_x_(0.0f),
      shift_pressed_(false),
      is_hovering_(false),
      is_pushed_(false),
      text_color_(kTextBoxTextColor) {}

void InputBox::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  // Add the focusable component to our node.
  focusable_ = strong_node->GetOrAdd<Focusable>();

  strong_node->OnDraw(std::bind_front(&InputBox::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&InputBox::Measure, this));

  strong_node->OnMouseHover([this](const Point&) {
    is_hovering_ = true;
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
          MoveCursorToPoint(point);
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
  scroll_x_ = 0.0f;
  EnsureCursorVisible();
  if (!node_.expired()) {
    node_.lock()->Invalidate();
  }
}

std::string InputBox::GetText() const { return text_; }

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
    outline_paint.setColor(0xFF3B82F6);  // Premium Focused Blue
    outline_paint.setStrokeWidth(2.0f);
  } else if (is_hovering_) {
    outline_paint.setColor(0xFF9CA3AF);  // Hovered grey
    outline_paint.setStrokeWidth(1.0f);
  } else {
    outline_paint.setColor(kTextBoxOutlineColor);  // Standard border
    outline_paint.setStrokeWidth(1.0f);
  }
  draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                          kTextBoxCornerRadius,
                                          kTextBoxCornerRadius, outline_paint);

  // 3. Draw Text with clipping
  float padding = 8.0f;
  draw_context.skia_canvas->save();

  // Clip to the client area (inside border)
  float clip_border = has_focus ? 2.0f : 1.0f;
  draw_context.skia_canvas->clipRect(SkRect::MakeXYWH(
      x + clip_border, y + clip_border, width - clip_border * 2.0f,
      height - clip_border * 2.0f));

  SkFontMetrics font_metrics;
  font_->getMetrics(&font_metrics);
  float font_height = font_metrics.fDescent - font_metrics.fAscent;

  // Vertically center text
  float text_y = y + (height - font_height) / 2.0f - font_metrics.fAscent;

  SkPaint text_paint;
  text_paint.setAntiAlias(true);
  text_paint.setColor(text_color_);

  float text_draw_x = x + padding - scroll_x_;
  draw_context.skia_canvas->drawString(SkString(text_.data(), text_.length()),
                                       text_draw_x, text_y, *font_, text_paint);

  // 4. Draw Cursor if focused
  if (has_focus) {
    float cursor_offset_x = 0.0f;
    if (cursor_index_ > 0) {
      SkRect bounds;
      font_->measureText(text_.data(), cursor_index_, SkTextEncoding::kUTF8,
                         &bounds);
      cursor_offset_x = bounds.width();
    }

    float cursor_x = text_draw_x + cursor_offset_x;
    SkPaint cursor_paint;
    cursor_paint.setAntiAlias(true);
    cursor_paint.setColor(kTextBoxTextColor);
    cursor_paint.setStyle(SkPaint::kFill_Style);

    float cursor_y_top = y + (height - font_height) / 2.0f;
    float cursor_y_bottom = cursor_y_top + font_height;

    draw_context.skia_canvas->drawRect(
        SkRect::MakeXYWH(cursor_x, cursor_y_top, 1.5f, font_height),
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
  float preferred_height = font_height + 16.0f;  // 8px padding top & bottom

  return {
      .width = CalculateMeasuredLength(width_mode, width, 150.0f),
      .height = CalculateMeasuredLength(height_mode, height, preferred_height)};
}

void InputBox::MoveCursorToPoint(const Point& point) {
  AssignDefaultFontIfUnassigned();
  float padding = 8.0f;
  float click_x = point.x - padding + scroll_x_;

  size_t best_index = 0;
  float best_distance = std::abs(click_x);

  size_t idx = 0;
  while (idx < text_.length()) {
    size_t char_len = GetNextUtf8CharLength(text_, idx);
    if (char_len == 0) break;
    idx += char_len;

    SkRect bounds;
    font_->measureText(text_.data(), idx, SkTextEncoding::kUTF8, &bounds);
    float char_x = bounds.width();
    float dist = std::abs(char_x - click_x);
    if (dist < best_distance) {
      best_distance = dist;
      best_index = idx;
    } else {
      break;
    }
  }
  cursor_index_ = best_index;
  EnsureCursorVisible();
}

void InputBox::EnsureCursorVisible() {
  AssignDefaultFontIfUnassigned();
  float padding = 8.0f;
  float cursor_offset_x = 0.0f;
  if (cursor_index_ > 0) {
    SkRect bounds;
    font_->measureText(text_.data(), cursor_index_, SkTextEncoding::kUTF8,
                       &bounds);
    cursor_offset_x = bounds.width();
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

  if (key == KeyCode::Backspace) {
    if (cursor_index_ > 0) {
      size_t prev_idx = GetPreviousUtf8CharIndex(text_, cursor_index_);
      std::string new_text =
          text_.substr(0, prev_idx) + text_.substr(cursor_index_);
      text_ = new_text;
      cursor_index_ = prev_idx;
      EnsureCursorVisible();
      NotifyTextChanged();
    }
    return;
  }

  if (key == KeyCode::Delete) {
    if (cursor_index_ < text_.length()) {
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
    if (cursor_index_ > 0) {
      cursor_index_ = GetPreviousUtf8CharIndex(text_, cursor_index_);
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    }
    return;
  }

  if (key == KeyCode::RightArrow) {
    if (cursor_index_ < text_.length()) {
      cursor_index_ += GetNextUtf8CharLength(text_, cursor_index_);
      EnsureCursorVisible();
      if (auto strong = node_.lock()) strong->Invalidate();
    }
    return;
  }

  if (key == KeyCode::Home) {
    cursor_index_ = 0;
    EnsureCursorVisible();
    if (auto strong = node_.lock()) strong->Invalidate();
    return;
  }

  if (key == KeyCode::End) {
    cursor_index_ = text_.length();
    EnsureCursorVisible();
    if (auto strong = node_.lock()) strong->Invalidate();
    return;
  }

  if (key == KeyCode::Enter) {
    NotifyEnterPressed();
    return;
  }

  char ascii = ScancodeToAscii(event.key, shift_pressed_);
  if (ascii != '\0') {
    std::string new_text =
        text_.substr(0, cursor_index_) + ascii + text_.substr(cursor_index_);
    text_ = new_text;
    cursor_index_ += 1;
    EnsureCursorVisible();
    NotifyTextChanged();
  }
}

void InputBox::HandleKeyUp(const window::KeyboardKeyEvent& event) {
  if (IsShiftKey(event.key)) {
    shift_pressed_ = false;
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

}  // namespace components
}  // namespace ui
}  // namespace perception
