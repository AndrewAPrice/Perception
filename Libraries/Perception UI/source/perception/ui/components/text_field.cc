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

#include "perception/ui/components/text_field.h"
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
template class UniqueIdentifiableType<ui::components::TextField>;

namespace ui {
namespace components {

TextField::TextField()
    : font_(nullptr),
      lines_({""}),
      cursor_line_(0),
      cursor_char_(0),
      selection_start_line_(0),
      selection_start_char_(0),
      shift_pressed_(false),
      ctrl_pressed_(false),
      is_hovering_(false),
      is_pushed_(false),
      text_color_(kTextBoxTextColor),
      word_wrap_(true),
      layout_is_dirty_(true),
      last_layout_width_(-1.0f) {}

void TextField::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  focusable_ = strong_node->GetOrAdd<Focusable>();

  strong_node->OnDraw(std::bind_front(&TextField::DrawOuter, this));

  strong_node->OnMouseHover([this](const Point& point) {
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
          if (focusable_) focusable_->Focus();
        }
      });

  focusable_->OnFocus([this]() {
    if (auto strong = node_.lock()) strong->Invalidate();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
  });

  focusable_->OnUnfocus([this]() {
    shift_pressed_ = false;
    ctrl_pressed_ = false;
    selection_start_line_ = cursor_line_;
    selection_start_char_ = cursor_char_;
    if (auto strong = node_.lock()) strong->Invalidate();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
  });

  focusable_->OnKeyDown(
      [this](const window::KeyboardKeyEvent& event) { HandleKeyDown(event); });

  focusable_->OnKeyUp(
      [this](const window::KeyboardKeyEvent& event) { HandleKeyUp(event); });
}

void TextField::SetInnerNode(std::shared_ptr<Node> inner_node) {
  inner_node_ = inner_node;
  if (!inner_node) return;
  inner_node->OnDraw(std::bind_front(&TextField::DrawInner, this));
  inner_node->SetMeasureFunction(
      std::bind_front(&TextField::MeasureInner, this));

  inner_node->OnMouseHover([this](const Point& point) {
    is_hovering_ = true;
    if (is_pushed_) MoveCursorToPoint(point, true);
    if (auto strong = node_.lock()) strong->Invalidate();
  });

  inner_node->OnMouseButtonDown(
      [this](const Point& point, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          is_pushed_ = true;
          if (focusable_) focusable_->Focus();
          MoveCursorToPoint(point, shift_pressed_);
          if (auto strong = node_.lock()) strong->Invalidate();
        }
      });

  inner_node->OnMouseButtonUp([this](const Point&, window::MouseButton button) {
    if (button == window::MouseButton::Left) {
      is_pushed_ = false;
      if (auto strong = node_.lock()) strong->Invalidate();
    }
  });
}

void TextField::SetText(std::string_view text) {
  layout_is_dirty_ = true;
  lines_.clear();
  std::string expanded_text;
  expanded_text.reserve(text.length());
  for (char c : text) {
    if (c == '\t') {
      expanded_text += "  ";
    } else {
      expanded_text += c;
    }
  }

  size_t start = 0;
  while (start < expanded_text.length()) {
    size_t newline = expanded_text.find('\n', start);
    if (newline == std::string_view::npos) {
      lines_.push_back(std::string(expanded_text.substr(start)));
      break;
    } else {
      lines_.push_back(
          std::string(expanded_text.substr(start, newline - start)));
      start = newline + 1;
    }
  }
  if (lines_.empty() ||
      (!expanded_text.empty() && expanded_text.back() == '\n')) {
    lines_.push_back("");
  }
  cursor_line_ = lines_.size() - 1;
  cursor_char_ = lines_.back().length();
  selection_start_line_ = cursor_line_;
  selection_start_char_ = cursor_char_;

  if (auto inner = inner_node_.lock()) {
    if (auto viewport = inner->GetParent().lock()) {
      viewport->SetOffset({0.0f, 0.0f});
    }
    inner->Remeasure();
    inner->Invalidate();
  }
  EnsureCursorVisible();
  if (auto strong = node_.lock()) {
    strong->Invalidate();
  }
}

std::string TextField::GetText() const {
  std::string res;
  for (size_t i = 0; i < lines_.size(); i++) {
    res += lines_[i];
    if (i + 1 < lines_.size()) res += '\n';
  }
  return res;
}

bool TextField::HasFocus() const {
  return focusable_ && focusable_->HasFocus();
}

void TextField::OnTextChanged(std::function<void(std::string_view)> handler) {
  on_text_changed_handlers_.push_back(handler);
}

void TextField::SetFont(SkFont* font) {
  if (font_ == font) return;
  font_ = font;
  layout_is_dirty_ = true;
  if (auto inner = inner_node_.lock()) {
    inner->Remeasure();
    inner->Invalidate();
  }
  if (auto strong = node_.lock()) {
    strong->Invalidate();
  }
}

SkFont* TextField::GetFont() const { return font_; }

void TextField::SetTextColor(uint32 color) {
  if (text_color_ == color) return;
  text_color_ = color;
  if (auto inner = inner_node_.lock()) {
    inner->Invalidate();
  }
}

uint32 TextField::GetTextColor() const { return text_color_; }

void TextField::SetWordWrap(bool word_wrap) {
  if (word_wrap_ == word_wrap) return;
  word_wrap_ = word_wrap;
  layout_is_dirty_ = true;
  if (auto inner = inner_node_.lock()) {
    inner->Remeasure();
    inner->Invalidate();
  }
  if (auto strong = node_.lock()) {
    strong->Invalidate();
  }
}

bool TextField::GetWordWrap() const { return word_wrap_; }

void TextField::DrawOuter(const DrawContext& draw_context) {
  bool has_focus = focusable_ && focusable_->HasFocus();
  InputBox::DrawTextBoxOuter(draw_context, has_focus, is_hovering_);
}

void TextField::DrawInner(const DrawContext& draw_context) {
  AssignDefaultFontIfUnassigned();
  LayoutLinesIfNeeded(last_layout_width_);

  const float& x = draw_context.area.origin.x;
  const float& y = draw_context.area.origin.y;

  bool has_focus = focusable_ && focusable_->HasFocus();

  float padding = kTextBoxPadding;
  draw_context.skia_canvas->save();

  SkFontMetrics font_metrics;
  font_->getMetrics(&font_metrics);
  float font_height = font_metrics.fDescent - font_metrics.fAscent;

  // Determine selection range
  bool has_sel = has_focus && (selection_start_line_ != cursor_line_ ||
                               selection_start_char_ != cursor_char_);
  size_t start_l = 0, start_c = 0, end_l = 0, end_c = 0;
  if (has_sel) {
    if (selection_start_line_ < cursor_line_ ||
        (selection_start_line_ == cursor_line_ &&
         selection_start_char_ < cursor_char_)) {
      start_l = selection_start_line_;
      start_c = selection_start_char_;
      end_l = cursor_line_;
      end_c = cursor_char_;
    } else {
      start_l = cursor_line_;
      start_c = cursor_char_;
      end_l = selection_start_line_;
      end_c = selection_start_char_;
    }
  }

  SkPaint sel_paint;
  sel_paint.setAntiAlias(true);
  sel_paint.setColor(kTextBoxSelectionColor);
  sel_paint.setStyle(SkPaint::kFill_Style);

  SkPaint text_paint;
  text_paint.setAntiAlias(true);
  text_paint.setColor(text_color_);

  const float clip_top = draw_context.clipping_bounds.origin.y;
  const float clip_bottom = clip_top + draw_context.clipping_bounds.size.height;

  for (size_t i = 0; i < laid_out_lines_.size(); i++) {
    const auto& laid_out = laid_out_lines_[i];
    float line_y_top = y + padding + i * font_height;
    if (line_y_top + font_height < clip_top) continue;
    if (line_y_top > clip_bottom) continue;

    float line_draw_x = x + padding;
    size_t p = laid_out.paragraph_index;

    std::string_view line_view = std::string_view(lines_[p]).substr(
        laid_out.start_char, laid_out.end_char - laid_out.start_char);

    if (has_sel && p >= start_l && p <= end_l) {
      size_t sel_p_start = (p == start_l) ? start_c : 0;
      size_t sel_p_end = (p == end_l) ? end_c : lines_[p].length();

      size_t inter_start = std::max(sel_p_start, laid_out.start_char);
      size_t inter_end = std::min(sel_p_end, laid_out.end_char);

      if (inter_start < inter_end ||
          (inter_start == inter_end && p < end_l &&
           laid_out.end_char == lines_[p].length())) {
        float sel_x_start = line_draw_x;
        if (inter_start > laid_out.start_char) {
          sel_x_start = line_draw_x +
                        font_->measureText(line_view.data(),
                                           inter_start - laid_out.start_char,
                                           SkTextEncoding::kUTF8);
        }
        float sel_x_end = line_draw_x;
        if (inter_end > laid_out.start_char) {
          sel_x_end =
              line_draw_x + font_->measureText(line_view.data(),
                                               inter_end - laid_out.start_char,
                                               SkTextEncoding::kUTF8);
        }
        if (inter_start == inter_end && p < end_l &&
            laid_out.end_char == lines_[p].length()) {
          sel_x_end += kTextFieldNewlineSelectionWidth;
        }
        draw_context.skia_canvas->drawRect(
            SkRect::MakeXYWH(
                sel_x_start, line_y_top,
                std::max(kTextFieldMinSelectionWidth, sel_x_end - sel_x_start),
                font_height),
            sel_paint);
      }
    }

    if (!line_view.empty()) {
      float text_draw_y = line_y_top - font_metrics.fAscent;
      draw_context.skia_canvas->drawString(
          SkString(line_view.data(), line_view.length()), line_draw_x,
          text_draw_y, *font_, text_paint);
    }

    if (has_focus && !has_sel && p == cursor_line_) {
      bool cursor_on_this_line = false;
      if (cursor_char_ >= laid_out.start_char &&
          cursor_char_ < laid_out.end_char) {
        cursor_on_this_line = true;
      } else if (cursor_char_ == laid_out.end_char &&
                 laid_out.end_char == lines_[p].length()) {
        cursor_on_this_line = true;
      }

      if (cursor_on_this_line) {
        float cursor_offset_x = 0.0f;
        if (cursor_char_ > laid_out.start_char) {
          cursor_offset_x = font_->measureText(
              line_view.data(), cursor_char_ - laid_out.start_char,
              SkTextEncoding::kUTF8);
        }
        float cursor_x = line_draw_x + cursor_offset_x;
        SkPaint cursor_paint;
        cursor_paint.setAntiAlias(true);
        cursor_paint.setColor(kTextBoxTextColor);
        cursor_paint.setStyle(SkPaint::kFill_Style);

        draw_context.skia_canvas->drawRect(
            SkRect::MakeXYWH(cursor_x, line_y_top, kTextBoxCursorWidth,
                             font_height),
            cursor_paint);
      }
    }
  }

  draw_context.skia_canvas->restore();
}

Size TextField::MeasureInner(float width, YGMeasureMode width_mode,
                             float height, YGMeasureMode height_mode) {
  AssignDefaultFontIfUnassigned();

  float max_width = 0.0f;
  if (width_mode == YGMeasureModeExactly || width_mode == YGMeasureModeAtMost) {
    max_width = width;
    if (max_width > kTextBoxPadding * 2.0f) {
      max_width -= kTextBoxPadding * 2.0f;
    }
    if (max_width < kTextFieldMinWrapWidth) max_width = kTextFieldMinWrapWidth;
  }
  if (!word_wrap_) max_width = 0.0f;

  LayoutLinesIfNeeded(max_width);

  SkFontMetrics metrics;
  font_->getMetrics(&metrics);
  float font_height = metrics.fDescent - metrics.fAscent;

  float preferred_height =
      laid_out_lines_.size() * font_height + kTextBoxPadding * 2.0f;

  float max_line_width = 0.0f;
  for (const auto& line : laid_out_lines_) {
    max_line_width = std::max(max_line_width, line.width);
  }
  float preferred_width = max_line_width + kTextBoxPadding * 2.0f;

  return {
      .width = CalculateMeasuredLength(width_mode, width, preferred_width),
      .height = CalculateMeasuredLength(height_mode, height, preferred_height)};
}

void TextField::MoveCursorToPoint(const Point& point, bool extend_selection) {
  AssignDefaultFontIfUnassigned();
  LayoutLinesIfNeeded(last_layout_width_);

  SkFontMetrics metrics;
  font_->getMetrics(&metrics);
  float font_height = metrics.fDescent - metrics.fAscent;

  float padding = kTextBoxPadding;
  float click_y = point.y - padding;
  int vis_line_idx = static_cast<int>(click_y / font_height);
  if (vis_line_idx < 0) vis_line_idx = 0;
  if (vis_line_idx >= static_cast<int>(laid_out_lines_.size())) {
    vis_line_idx = static_cast<int>(laid_out_lines_.size()) - 1;
  }

  if (laid_out_lines_.empty()) {
    cursor_line_ = 0;
    cursor_char_ = 0;
  } else {
    const auto& laid_out = laid_out_lines_[vis_line_idx];
    cursor_line_ = laid_out.paragraph_index;

    float click_x = point.x - padding;
    std::string_view line_view =
        std::string_view(lines_[cursor_line_])
            .substr(laid_out.start_char,
                    laid_out.end_char - laid_out.start_char);

    cursor_char_ =
        laid_out.start_char + FindClosestCursorIndex(line_view, click_x, font_);
  }

  if (!extend_selection) {
    selection_start_line_ = cursor_line_;
    selection_start_char_ = cursor_char_;
  }
  EnsureCursorVisible();
}

void TextField::EnsureCursorVisible() {
  AssignDefaultFontIfUnassigned();
  LayoutLinesIfNeeded(last_layout_width_);

  SkFontMetrics metrics;
  font_->getMetrics(&metrics);
  float font_height = metrics.fDescent - metrics.fAscent;

  float padding = kTextBoxPadding;
  int vis_idx = 0;
  float cursor_offset_x = padding;

  for (size_t i = 0; i < laid_out_lines_.size(); i++) {
    const auto& laid_out = laid_out_lines_[i];
    if (laid_out.paragraph_index == cursor_line_) {
      if ((cursor_char_ >= laid_out.start_char &&
           cursor_char_ < laid_out.end_char) ||
          (cursor_char_ == laid_out.end_char &&
           laid_out.end_char == lines_[cursor_line_].length())) {
        vis_idx = i;
        if (cursor_char_ > laid_out.start_char) {
          std::string_view lv =
              std::string_view(lines_[cursor_line_])
                  .substr(laid_out.start_char,
                          laid_out.end_char - laid_out.start_char);
          cursor_offset_x +=
              font_->measureText(lv.data(), cursor_char_ - laid_out.start_char,
                                 SkTextEncoding::kUTF8);
        }
        break;
      }
    }
  }

  float cursor_y_top = padding + vis_idx * font_height;
  float cursor_y_bottom = cursor_y_top + font_height;

  if (auto inner = inner_node_.lock()) {
    if (auto viewport = inner->GetParent().lock()) {
      Size viewport_size = viewport->GetSize();
      if (viewport_size.width <= 0.0f || viewport_size.height <= 0.0f) return;

      Point offset = viewport->GetOffset();
      Point new_offset = offset;

      if (viewport_size.width > padding * 2.0f) {
        if (cursor_offset_x < offset.x + padding) {
          new_offset.x = std::max(0.0f, cursor_offset_x - padding);
        } else if (cursor_offset_x > offset.x + viewport_size.width - padding) {
          new_offset.x = std::max(
              0.0f, cursor_offset_x - (viewport_size.width - padding * 2.0f));
        }
      }

      if (viewport_size.height > padding * 2.0f) {
        if (cursor_y_top < offset.y + padding) {
          new_offset.y = std::max(0.0f, cursor_y_top - padding);
        } else if (cursor_y_bottom >
                   offset.y + viewport_size.height - padding) {
          new_offset.y = std::max(
              0.0f, cursor_y_bottom - (viewport_size.height - padding * 2.0f));
        }
      }

      if (new_offset != offset) {
        viewport->SetOffset(new_offset);
      }
    }
  }
}

void TextField::DeleteSelection() {
  if (selection_start_line_ == cursor_line_ &&
      selection_start_char_ == cursor_char_) {
    return;
  }
  size_t start_l, start_c, end_l, end_c;
  if (selection_start_line_ < cursor_line_ ||
      (selection_start_line_ == cursor_line_ &&
       selection_start_char_ < cursor_char_)) {
    start_l = selection_start_line_;
    start_c = selection_start_char_;
    end_l = cursor_line_;
    end_c = cursor_char_;
  } else {
    start_l = cursor_line_;
    start_c = cursor_char_;
    end_l = selection_start_line_;
    end_c = selection_start_char_;
  }

  if (start_l == end_l) {
    lines_[start_l] =
        lines_[start_l].substr(0, start_c) + lines_[start_l].substr(end_c);
  } else {
    lines_[start_l] =
        lines_[start_l].substr(0, start_c) + lines_[end_l].substr(end_c);
    if (end_l > start_l + 1) {
      lines_.erase(lines_.begin() + start_l + 1, lines_.begin() + end_l);
    }
    lines_.erase(lines_.begin() + start_l + 1);
  }
  cursor_line_ = start_l;
  cursor_char_ = start_c;
  selection_start_line_ = start_l;
  selection_start_char_ = start_c;
  layout_is_dirty_ = true;
}

void TextField::InsertString(std::string_view str) {
  DeleteSelection();
  if (str.empty()) return;

  std::string expanded_str;
  expanded_str.reserve(str.length());
  for (char c : str) {
    if (c == '\t') {
      expanded_str += "  ";
    } else {
      expanded_str += c;
    }
  }

  std::vector<std::string> parts;
  size_t start = 0;
  while (start < expanded_str.length()) {
    size_t newline = expanded_str.find('\n', start);
    if (newline == std::string_view::npos) {
      parts.push_back(std::string(expanded_str.substr(start)));
      break;
    } else {
      parts.push_back(std::string(expanded_str.substr(start, newline - start)));
      start = newline + 1;
    }
  }
  if (parts.empty() || (!expanded_str.empty() && expanded_str.back() == '\n')) {
    parts.push_back("");
  }

  if (parts.size() == 1) {
    lines_[cursor_line_].insert(cursor_char_, parts[0]);
    cursor_char_ += parts[0].length();
  } else {
    std::string remainder = lines_[cursor_line_].substr(cursor_char_);
    lines_[cursor_line_] =
        lines_[cursor_line_].substr(0, cursor_char_) + parts[0];
    for (size_t i = 1; i < parts.size(); i++) {
      if (i == parts.size() - 1) {
        lines_.insert(lines_.begin() + cursor_line_ + i, parts[i] + remainder);
        cursor_line_ += i;
        cursor_char_ = parts[i].length();
      } else {
        lines_.insert(lines_.begin() + cursor_line_ + i, parts[i]);
      }
    }
  }
  selection_start_line_ = cursor_line_;
  selection_start_char_ = cursor_char_;
  layout_is_dirty_ = true;
  EnsureCursorVisible();
  if (auto inner = inner_node_.lock()) {
    inner->Remeasure();
    inner->Invalidate();
  }
}

std::string TextField::GetSelectedText() const {
  if (selection_start_line_ == cursor_line_ &&
      selection_start_char_ == cursor_char_) {
    return "";
  }
  size_t start_l, start_c, end_l, end_c;
  if (selection_start_line_ < cursor_line_ ||
      (selection_start_line_ == cursor_line_ &&
       selection_start_char_ < cursor_char_)) {
    start_l = selection_start_line_;
    start_c = selection_start_char_;
    end_l = cursor_line_;
    end_c = cursor_char_;
  } else {
    start_l = cursor_line_;
    start_c = cursor_char_;
    end_l = selection_start_line_;
    end_c = selection_start_char_;
  }

  if (start_l == end_l) {
    return lines_[start_l].substr(start_c, end_c - start_c);
  }

  std::string res = lines_[start_l].substr(start_c) + "\n";
  for (size_t i = start_l + 1; i < end_l; i++) {
    res += lines_[i] + "\n";
  }
  res += lines_[end_l].substr(0, end_c);
  return res;
}

void TextField::HandleKeyDown(const window::KeyboardKeyEvent& event) {
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
    if (selection_start_line_ != cursor_line_ ||
        selection_start_char_ != cursor_char_) {
      DeleteSelection();
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_char_ > 0) {
      size_t prev_idx =
          GetPreviousUtf8CharIndex(lines_[cursor_line_], cursor_char_);
      lines_[cursor_line_].erase(prev_idx, cursor_char_ - prev_idx);
      cursor_char_ = prev_idx;
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_line_ > 0) {
      size_t prev_len = lines_[cursor_line_ - 1].length();
      lines_[cursor_line_ - 1] += lines_[cursor_line_];
      lines_.erase(lines_.begin() + cursor_line_);
      cursor_line_ -= 1;
      cursor_char_ = prev_len;
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
      EnsureCursorVisible();
      NotifyTextChanged();
    }
    return;
  }

  if (key == KeyCode::Delete) {
    if (selection_start_line_ != cursor_line_ ||
        selection_start_char_ != cursor_char_) {
      DeleteSelection();
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_char_ < lines_[cursor_line_].length()) {
      size_t next_len =
          GetNextUtf8CharLength(lines_[cursor_line_], cursor_char_);
      lines_[cursor_line_].erase(cursor_char_, next_len);
      EnsureCursorVisible();
      NotifyTextChanged();
    } else if (cursor_line_ + 1 < lines_.size()) {
      lines_[cursor_line_] += lines_[cursor_line_ + 1];
      lines_.erase(lines_.begin() + cursor_line_ + 1);
      EnsureCursorVisible();
      NotifyTextChanged();
    }
    return;
  }

  if (key == KeyCode::Enter) {
    InsertString("\n");
    NotifyTextChanged();
    return;
  }

  if (key == KeyCode::Tab) {
    size_t start_l = std::min(selection_start_line_, cursor_line_);
    size_t end_l = std::max(selection_start_line_, cursor_line_);

    if (shift_pressed_) {
      for (size_t l = start_l; l <= end_l; l++) {
        size_t spaces = 0;
        if (lines_[l].starts_with("  ")) {
          spaces = 2;
        } else if (lines_[l].starts_with(" ")) {
          spaces = 1;
        }
        if (spaces > 0) {
          lines_[l].erase(0, spaces);
          if (l == cursor_line_) {
            if (cursor_char_ >= spaces)
              cursor_char_ -= spaces;
            else
              cursor_char_ = 0;
          }
          if (l == selection_start_line_) {
            if (selection_start_char_ >= spaces)
              selection_start_char_ -= spaces;
            else
              selection_start_char_ = 0;
          }
        }
      }
    } else {
      if (start_l < end_l) {
        for (size_t l = start_l; l <= end_l; l++) {
          lines_[l].insert(0, "  ");
          if (l == cursor_line_) cursor_char_ += 2;
          if (l == selection_start_line_) selection_start_char_ += 2;
        }
      } else {
        InsertString("  ");
      }
    }

    EnsureCursorVisible();
    NotifyTextChanged();
    return;
  }

  if (key == KeyCode::LeftArrow) {
    if (shift_pressed_) {
      if (cursor_char_ > 0) {
        cursor_char_ =
            GetPreviousUtf8CharIndex(lines_[cursor_line_], cursor_char_);
      } else if (cursor_line_ > 0) {
        cursor_line_ -= 1;
        cursor_char_ = lines_[cursor_line_].length();
      }
    } else {
      if (selection_start_line_ != cursor_line_ ||
          selection_start_char_ != cursor_char_) {
        if (selection_start_line_ < cursor_line_ ||
            (selection_start_line_ == cursor_line_ &&
             selection_start_char_ < cursor_char_)) {
          cursor_line_ = selection_start_line_;
          cursor_char_ = selection_start_char_;
        }
      } else if (cursor_char_ > 0) {
        cursor_char_ =
            GetPreviousUtf8CharIndex(lines_[cursor_line_], cursor_char_);
      } else if (cursor_line_ > 0) {
        cursor_line_ -= 1;
        cursor_char_ = lines_[cursor_line_].length();
      }
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (key == KeyCode::RightArrow) {
    if (shift_pressed_) {
      if (cursor_char_ < lines_[cursor_line_].length()) {
        cursor_char_ +=
            GetNextUtf8CharLength(lines_[cursor_line_], cursor_char_);
      } else if (cursor_line_ + 1 < lines_.size()) {
        cursor_line_ += 1;
        cursor_char_ = 0;
      }
    } else {
      if (selection_start_line_ != cursor_line_ ||
          selection_start_char_ != cursor_char_) {
        if (cursor_line_ < selection_start_line_ ||
            (cursor_line_ == selection_start_line_ &&
             cursor_char_ < selection_start_char_)) {
          cursor_line_ = selection_start_line_;
          cursor_char_ = selection_start_char_;
        }
      } else if (cursor_char_ < lines_[cursor_line_].length()) {
        cursor_char_ +=
            GetNextUtf8CharLength(lines_[cursor_line_], cursor_char_);
      } else if (cursor_line_ + 1 < lines_.size()) {
        cursor_line_ += 1;
        cursor_char_ = 0;
      }
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (key == KeyCode::UpArrow) {
    AssignDefaultFontIfUnassigned();
    LayoutLinesIfNeeded(last_layout_width_);

    int vis_idx = -1;
    for (size_t i = 0; i < laid_out_lines_.size(); i++) {
      const auto& laid_out = laid_out_lines_[i];
      if (laid_out.paragraph_index == cursor_line_) {
        if ((cursor_char_ >= laid_out.start_char &&
             cursor_char_ < laid_out.end_char) ||
            (cursor_char_ == laid_out.end_char &&
             laid_out.end_char == lines_[cursor_line_].length())) {
          vis_idx = i;
          break;
        }
      }
    }

    if (vis_idx > 0) {
      const auto& cur_laid = laid_out_lines_[vis_idx];
      std::string_view cur_lv =
          std::string_view(lines_[cur_laid.paragraph_index])
              .substr(cur_laid.start_char,
                      cur_laid.end_char - cur_laid.start_char);
      float target_x =
          font_->measureText(cur_lv.data(), cursor_char_ - cur_laid.start_char,
                             SkTextEncoding::kUTF8);

      int target_vis = vis_idx - 1;
      const auto& tgt_laid = laid_out_lines_[target_vis];
      std::string_view tgt_lv =
          std::string_view(lines_[tgt_laid.paragraph_index])
              .substr(tgt_laid.start_char,
                      tgt_laid.end_char - tgt_laid.start_char);

      cursor_line_ = tgt_laid.paragraph_index;
      cursor_char_ =
          tgt_laid.start_char + FindClosestCursorIndex(tgt_lv, target_x, font_);
    } else {
      cursor_line_ = 0;
      cursor_char_ = 0;
    }

    if (!shift_pressed_) {
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (key == KeyCode::DownArrow) {
    AssignDefaultFontIfUnassigned();
    LayoutLinesIfNeeded(last_layout_width_);

    int vis_idx = -1;
    for (size_t i = 0; i < laid_out_lines_.size(); i++) {
      const auto& laid_out = laid_out_lines_[i];
      if (laid_out.paragraph_index == cursor_line_) {
        if ((cursor_char_ >= laid_out.start_char &&
             cursor_char_ < laid_out.end_char) ||
            (cursor_char_ == laid_out.end_char &&
             laid_out.end_char == lines_[cursor_line_].length())) {
          vis_idx = i;
          break;
        }
      }
    }

    if (vis_idx >= 0 &&
        vis_idx + 1 < static_cast<int>(laid_out_lines_.size())) {
      const auto& cur_laid = laid_out_lines_[vis_idx];
      std::string_view cur_lv =
          std::string_view(lines_[cur_laid.paragraph_index])
              .substr(cur_laid.start_char,
                      cur_laid.end_char - cur_laid.start_char);
      float target_x =
          font_->measureText(cur_lv.data(), cursor_char_ - cur_laid.start_char,
                             SkTextEncoding::kUTF8);

      int target_vis = vis_idx + 1;
      const auto& tgt_laid = laid_out_lines_[target_vis];
      std::string_view tgt_lv =
          std::string_view(lines_[tgt_laid.paragraph_index])
              .substr(tgt_laid.start_char,
                      tgt_laid.end_char - tgt_laid.start_char);

      cursor_line_ = tgt_laid.paragraph_index;
      cursor_char_ =
          tgt_laid.start_char + FindClosestCursorIndex(tgt_lv, target_x, font_);
    } else {
      cursor_line_ = lines_.size() - 1;
      cursor_char_ = lines_.back().length();
    }

    if (!shift_pressed_) {
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (key == KeyCode::Home) {
    if (ctrl_pressed_) {
      cursor_line_ = 0;
      cursor_char_ = 0;
    } else {
      AssignDefaultFontIfUnassigned();
      LayoutLinesIfNeeded(last_layout_width_);

      int vis_idx = -1;
      for (size_t i = 0; i < laid_out_lines_.size(); i++) {
        const auto& laid_out = laid_out_lines_[i];
        if (laid_out.paragraph_index == cursor_line_) {
          if ((cursor_char_ >= laid_out.start_char &&
               cursor_char_ < laid_out.end_char) ||
              (cursor_char_ == laid_out.end_char &&
               laid_out.end_char == lines_[cursor_line_].length())) {
            vis_idx = i;
            break;
          }
        }
      }
      if (vis_idx >= 0) {
        cursor_char_ = laid_out_lines_[vis_idx].start_char;
      } else {
        cursor_char_ = 0;
      }
    }
    if (!shift_pressed_) {
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (key == KeyCode::End) {
    if (ctrl_pressed_) {
      cursor_line_ = lines_.size() - 1;
      cursor_char_ = lines_.back().length();
    } else {
      AssignDefaultFontIfUnassigned();
      LayoutLinesIfNeeded(last_layout_width_);

      int vis_idx = -1;
      for (size_t i = 0; i < laid_out_lines_.size(); i++) {
        const auto& laid_out = laid_out_lines_[i];
        if (laid_out.paragraph_index == cursor_line_) {
          if ((cursor_char_ >= laid_out.start_char &&
               cursor_char_ < laid_out.end_char) ||
              (cursor_char_ == laid_out.end_char &&
               laid_out.end_char == lines_[cursor_line_].length())) {
            vis_idx = i;
            break;
          }
        }
      }
      if (vis_idx >= 0) {
        cursor_char_ = laid_out_lines_[vis_idx].end_char;
      } else {
        cursor_char_ = lines_[cursor_line_].length();
      }
    }
    if (!shift_pressed_) {
      selection_start_line_ = cursor_line_;
      selection_start_char_ = cursor_char_;
    }
    EnsureCursorVisible();
    if (auto inner = inner_node_.lock()) inner->Invalidate();
    return;
  }

  if (ctrl_pressed_) {
    char ascii = ScancodeToAscii(event.key, false);
    if (ascii == 'c' || ascii == 'C') {
      std::string sel = GetSelectedText();
      if (!sel.empty()) {
        ::perception::SetClipboard(sel);
      }
    } else if (ascii == 'x' || ascii == 'X') {
      std::string sel = GetSelectedText();
      if (!sel.empty()) {
        ::perception::SetClipboard(sel);
        DeleteSelection();
        EnsureCursorVisible();
        NotifyTextChanged();
      }
    } else if (ascii == 'v' || ascii == 'V') {
      auto status_or_val = ::perception::GetClipboard();
      if (status_or_val.Ok()) {
        std::string clipboard_str = status_or_val->ToString();
        if (!clipboard_str.empty()) {
          InsertString(clipboard_str);
          NotifyTextChanged();
        }
      }
    }
    return;
  }

  char ascii = ScancodeToAscii(event.key, shift_pressed_);
  if (ascii != '\0') {
    InsertString(std::string(1, ascii));
    NotifyTextChanged();
  }
}

void TextField::HandleKeyUp(const window::KeyboardKeyEvent& event) {
  if (IsShiftKey(event.key)) {
    shift_pressed_ = false;
  } else if (IsControlKey(event.key)) {
    ctrl_pressed_ = false;
  }
}

void TextField::NotifyTextChanged() {
  layout_is_dirty_ = true;
  std::string full_text = GetText();
  for (auto& handler : on_text_changed_handlers_) {
    handler(full_text);
  }
  if (auto inner = inner_node_.lock()) {
    inner->Remeasure();
    inner->Invalidate();
  }
  if (auto strong = node_.lock()) {
    strong->Invalidate();
  }
}

void TextField::AssignDefaultFontIfUnassigned() {
  if (font_ == nullptr) font_ = GetBook12UiFont();
}

void TextField::LayoutLinesIfNeeded(float max_width) {
  if (!layout_is_dirty_ && max_width == last_layout_width_) {
    if (!laid_out_lines_.empty()) return;
  }

  AssignDefaultFontIfUnassigned();
  laid_out_lines_.clear();
  last_layout_width_ = max_width;
  layout_is_dirty_ = false;

  for (size_t p = 0; p < lines_.size(); p++) {
    const std::string& para = lines_[p];
    if (para.empty()) {
      laid_out_lines_.push_back({p, 0, 0, 0.0f});
      continue;
    }

    if (!word_wrap_ || max_width <= 0.0f) {
      float w =
          font_->measureText(para.data(), para.length(), SkTextEncoding::kUTF8);
      laid_out_lines_.push_back({p, 0, para.length(), w});
      continue;
    }

    size_t line_start = 0;
    size_t idx = 0;
    while (idx < para.length()) {
      size_t next_space = para.find(' ', idx);
      size_t word_end =
          (next_space == std::string::npos) ? para.length() : next_space + 1;

      std::string_view test_line =
          std::string_view(para).substr(line_start, word_end - line_start);
      float w = font_->measureText(test_line.data(), test_line.length(),
                                   SkTextEncoding::kUTF8);

      if (w <= max_width) {
        idx = word_end;
      } else {
        if (idx == line_start) {
          size_t c_idx = line_start;
          while (c_idx < word_end) {
            size_t c_len = GetNextUtf8CharLength(para, c_idx);
            if (c_len == 0) break;
            std::string_view c_line = std::string_view(para).substr(
                line_start, (c_idx + c_len) - line_start);
            float cw = font_->measureText(c_line.data(), c_line.length(),
                                          SkTextEncoding::kUTF8);
            if (cw <= max_width || c_idx == line_start) {
              c_idx += c_len;
            } else {
              std::string_view final_line =
                  std::string_view(para).substr(line_start, c_idx - line_start);
              float fw =
                  font_->measureText(final_line.data(), final_line.length(),
                                     SkTextEncoding::kUTF8);
              laid_out_lines_.push_back({p, line_start, c_idx, fw});
              line_start = c_idx;
            }
          }
          idx = word_end;
        } else {
          std::string_view final_line =
              std::string_view(para).substr(line_start, idx - line_start);
          float fw = font_->measureText(final_line.data(), final_line.length(),
                                        SkTextEncoding::kUTF8);
          laid_out_lines_.push_back({p, line_start, idx, fw});
          line_start = idx;
        }
      }
    }

    if (line_start < para.length()) {
      std::string_view final_line =
          std::string_view(para).substr(line_start, para.length() - line_start);
      float fw = font_->measureText(final_line.data(), final_line.length(),
                                    SkTextEncoding::kUTF8);
      laid_out_lines_.push_back({p, line_start, para.length(), fw});
    }
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
