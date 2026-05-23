// Copyright 2024 Google LLC
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

#include "perception/ui/components/label.h"

#include <memory>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/measurements.h"
#include "perception/ui/theme.h"

namespace perception {
template class UniqueIdentifiableType<ui::components::Label>;

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

void RemoveLastUtf8Character(std::string& s) {
  if (s.empty()) return;
  s.pop_back();
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
    s.pop_back();
  }
}

}  // namespace

Label::Label()
    : font_(nullptr),
      color_(kLabelTextColor),
      text_alignment_(TextAlignment::TopLeft),
      text_needs_realignment_(true),
      max_lines_(0),
      truncation_mode_(TruncationMode::None),
      last_layout_width_(-1.0f),
      layout_is_dirty_(true) {}

void Label::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();
  strong_node->OnDraw(std::bind_front(&Label::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&Label::Measure, this));
}

void Label::SetTextAlignment(TextAlignment text_alignment) {
  if (text_alignment == text_alignment_) return;
  text_alignment_ = text_alignment;
  if (!node_.expired()) node_.lock()->Invalidate();
}

TextAlignment Label::GetTextAlignment() { return text_alignment_; }

void Label::SetFont(SkFont* font) {
  if (font_ == font) return;
  font_ = font;
  text_needs_realignment_ = true;
  layout_is_dirty_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

SkFont* Label::GetFont() const { return font_; }

void Label::SetColor(uint32_t color) {
  if (color_ == color) return;
  color_ = color;
  if (!node_.expired()) node_.lock()->Invalidate();
}

uint32_t Label::GetColor() const { return color_; }

void Label::SetText(std::string_view text) {
  if (text_ == text) return;
  text_ = text;
  text_needs_realignment_ = true;
  layout_is_dirty_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

std::string_view Label::GetText() const { return text_; }

void Label::SetMaxLines(int max_lines) {
  if (max_lines_ == max_lines) return;
  max_lines_ = max_lines;
  layout_is_dirty_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

int Label::GetMaxLines() const { return max_lines_; }

void Label::SetTruncationMode(TruncationMode truncation_mode) {
  if (truncation_mode_ == truncation_mode) return;
  truncation_mode_ = truncation_mode;
  layout_is_dirty_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

Label::TruncationMode Label::GetTruncationMode() const {
  return truncation_mode_;
}

void Label::LayoutText(float max_width) {
  if (!layout_is_dirty_ && max_width == last_layout_width_) {
    return;
  }

  AssignDefaultFontIfUnassigned();

  // Check for fast path: no newlines and fits in constraint
  if (text_.find('\n') == std::string::npos) {
    SkRect bounds;
    font_->measureText(text_.data(), text_.length(), SkTextEncoding::kUTF8,
                       &bounds);
    if (bounds.width() <= max_width || max_width <= 0.0f) {
      laid_out_lines_.resize(1);
      laid_out_lines_[0].text_view = text_;
      laid_out_lines_[0].mutated_text.clear();
      laid_out_lines_[0].size = {.width = bounds.width(),
                                 .height = bounds.height()};
      last_layout_width_ = max_width;
      layout_is_dirty_ = false;
      return;
    }
  }

  laid_out_lines_.clear();
  last_layout_width_ = max_width;
  layout_is_dirty_ = false;

  // Split text into paragraphs by '\n'
  std::vector<std::string_view> paragraphs;
  size_t start = 0;
  while (true) {
    size_t pos = text_.find('\n', start);
    if (pos == std::string::npos) {
      paragraphs.push_back(std::string_view(text_).substr(start));
      break;
    }
    paragraphs.push_back(std::string_view(text_).substr(start, pos - start));
    start = pos + 1;
  }

  for (const auto& paragraph : paragraphs) {
    if (paragraph.empty()) {
      laid_out_lines_.push_back(
          {.text_view = "", .mutated_text = "", .size = {0.0f, 0.0f}});
      continue;
    }

    if (max_width <= 0.0f) {
      SkRect bounds;
      font_->measureText(paragraph.data(), paragraph.length(),
                         SkTextEncoding::kUTF8, &bounds);
      laid_out_lines_.push_back(
          {.text_view = paragraph,
           .mutated_text = "",
           .size = {.width = bounds.width(), .height = bounds.height()}});
      continue;
    }

    // Word wrapping logic
    size_t idx = 0;
    size_t line_start = 0;
    size_t line_len = 0;

    while (idx < paragraph.length()) {
      // Find next space delimiter
      size_t next_space = paragraph.find(' ', idx);
      std::string_view word;
      size_t word_end;
      if (next_space == std::string::npos) {
        word = paragraph.substr(idx);
        word_end = paragraph.length();
      } else {
        word = paragraph.substr(idx, next_space - idx + 1);
        word_end = next_space + 1;
      }

      // Test line with this word added
      std::string_view test_line =
          paragraph.substr(line_start, line_len + word.length());
      SkRect bounds;
      font_->measureText(test_line.data(), test_line.length(),
                         SkTextEncoding::kUTF8, &bounds);

      if (bounds.width() <= max_width) {
        line_len += word.length();
        idx = word_end;
      } else {
        if (line_len == 0) {
          // The word itself is too long for the line, break by character
          size_t char_idx = 0;
          while (char_idx < word.length()) {
            size_t char_len = GetNextUtf8CharLength(word, char_idx);
            if (char_len == 0) break;

            std::string_view test_char_line =
                paragraph.substr(line_start, line_len + char_len);
            SkRect char_bounds;
            font_->measureText(test_char_line.data(), test_char_line.length(),
                               SkTextEncoding::kUTF8, &char_bounds);

            if (char_bounds.width() <= max_width || line_len == 0) {
              line_len += char_len;
              char_idx += char_len;
            } else {
              // Commit current line
              std::string_view line_to_push =
                  paragraph.substr(line_start, line_len);
              while (!line_to_push.empty() && line_to_push.back() == ' ') {
                line_to_push.remove_suffix(1);
              }
              SkRect final_bounds;
              font_->measureText(line_to_push.data(), line_to_push.length(),
                                 SkTextEncoding::kUTF8, &final_bounds);
              laid_out_lines_.push_back(
                  {.text_view = line_to_push,
                   .mutated_text = "",
                   .size = {.width = final_bounds.width(),
                            .height = final_bounds.height()}});
              line_start = line_start + line_len;
              line_len = 0;
            }
          }
          idx = word_end;
        } else {
          // Commit current line and try adding word to next line
          std::string_view line_to_push =
              paragraph.substr(line_start, line_len);
          while (!line_to_push.empty() && line_to_push.back() == ' ') {
            line_to_push.remove_suffix(1);
          }
          SkRect final_bounds;
          font_->measureText(line_to_push.data(), line_to_push.length(),
                             SkTextEncoding::kUTF8, &final_bounds);
          laid_out_lines_.push_back(
              {.text_view = line_to_push,
               .mutated_text = "",
               .size = {.width = final_bounds.width(),
                        .height = final_bounds.height()}});
          line_start = idx;
          line_len = 0;
        }
      }
    }
    if (line_len > 0) {
      std::string_view line_to_push = paragraph.substr(line_start, line_len);
      while (!line_to_push.empty() && line_to_push.back() == ' ') {
        line_to_push.remove_suffix(1);
      }
      SkRect final_bounds;
      font_->measureText(line_to_push.data(), line_to_push.length(),
                         SkTextEncoding::kUTF8, &final_bounds);
      laid_out_lines_.push_back({.text_view = line_to_push,
                                 .mutated_text = "",
                                 .size = {.width = final_bounds.width(),
                                          .height = final_bounds.height()}});
    }
  }

  // Apply maximum lines limit
  bool truncated = false;
  if (max_lines_ > 0 &&
      laid_out_lines_.size() > static_cast<size_t>(max_lines_)) {
    laid_out_lines_.resize(max_lines_);
    truncated = true;
  }

  // Apply truncation mode / ellipsis
  if (!laid_out_lines_.empty()) {
    auto& last_line = laid_out_lines_.back();
    bool overflows = last_line.size.width > max_width;

    if (truncation_mode_ == TruncationMode::Ellipsis) {
      if (truncated || overflows) {
        std::string text_part(last_line.text_view);
        while (!text_part.empty()) {
          std::string test_str = text_part + "…";
          SkRect ell_bounds;
          font_->measureText(test_str.data(), test_str.length(),
                             SkTextEncoding::kUTF8, &ell_bounds);
          if (ell_bounds.width() <= max_width) {
            last_line.mutated_text = test_str;
            last_line.size = {.width = ell_bounds.width(),
                              .height = ell_bounds.height()};
            break;
          }
          RemoveLastUtf8Character(text_part);
        }
        if (text_part.empty()) {
          SkRect ell_bounds;
          font_->measureText("…", 3, SkTextEncoding::kUTF8, &ell_bounds);
          last_line.mutated_text = "…";
          last_line.size = {.width = ell_bounds.width(),
                            .height = ell_bounds.height()};
        }
      }
    } else if (truncation_mode_ == TruncationMode::LastWholeCharacter) {
      if (overflows) {
        std::string text_part(last_line.text_view);
        while (!text_part.empty()) {
          SkRect char_bounds;
          font_->measureText(text_part.data(), text_part.length(),
                             SkTextEncoding::kUTF8, &char_bounds);
          if (char_bounds.width() <= max_width) {
            last_line.mutated_text = text_part;
            last_line.size = {.width = char_bounds.width(),
                              .height = char_bounds.height()};
            break;
          }
          RemoveLastUtf8Character(text_part);
        }
      }
    }
  }
}

void Label::Draw(const DrawContext& draw_context) {
  AssignDefaultFontIfUnassigned();

  if (draw_context.area.size != size_) {
    size_ = draw_context.area.size;
    layout_is_dirty_ = true;
  }

  LayoutText(size_.width);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(color_);

  SkFontMetrics font_metrics;
  float line_height = font_->getMetrics(&font_metrics);

  // Calculate the bounding box of laid out lines
  float block_width = 0.0f;
  for (const auto& line : laid_out_lines_) {
    block_width = std::max(block_width, line.size.width);
  }

  float block_height = 0.0f;
  if (!laid_out_lines_.empty()) {
    if (laid_out_lines_.size() == 1) {
      block_height = laid_out_lines_[0].size.height;
    } else {
      block_height = (laid_out_lines_.size() - 1) * line_height +
                     (font_metrics.fDescent - font_metrics.fAscent);
    }
  }

  Size block_size = {.width = block_width, .height = block_height};
  Point block_origin = CalculateAlignment(block_size, size_, text_alignment_);

  for (size_t i = 0; i < laid_out_lines_.size(); ++i) {
    const auto& line = laid_out_lines_[i];
    std::string_view line_text = line.GetText();
    if (line_text.empty()) continue;

    float line_x = draw_context.area.origin.x +
                   CalculateAlignment(line.size, size_, text_alignment_).x;
    float line_y = draw_context.area.origin.y + block_origin.y -
                   font_metrics.fAscent + i * line_height;

    draw_context.skia_canvas->drawString(
        SkString(line_text.data(), line_text.length()), line_x, line_y, *font_,
        paint);
  }
}

Size Label::Measure(float width, YGMeasureMode width_mode, float height,
                    YGMeasureMode height_mode) {
  if (text_.empty()) {
    return {.width = CalculateMeasuredLength(width_mode, width, 0.0f),
            .height = CalculateMeasuredLength(height_mode, height, 0.0f)};
  }

  AssignDefaultFontIfUnassigned();

  float max_width = (width_mode == YGMeasureModeUndefined) ? 0.0f : width;
  if (!node_.expired()) {
    auto strong_node = node_.lock();
    auto layout = strong_node->GetLayout();
    auto max_width_val = layout.GetMaxWidth();
    if (max_width_val.unit == YGUnitPoint) {
      max_width = (max_width > 0.0f) ? std::min(max_width, max_width_val.value)
                                     : max_width_val.value;
    }
    auto width_val = layout.GetWidth();
    if (width_val.unit == YGUnitPoint) {
      max_width = (max_width > 0.0f) ? std::min(max_width, width_val.value)
                                     : width_val.value;
    }
  }
  LayoutText(max_width);

  // Calculate the bounding box of laid out lines
  float block_width = 0.0f;
  for (const auto& line : laid_out_lines_) {
    block_width = std::max(block_width, line.size.width);
  }

  SkFontMetrics font_metrics;
  float line_height = font_->getMetrics(&font_metrics);

  float block_height = 0.0f;
  if (!laid_out_lines_.empty()) {
    if (laid_out_lines_.size() == 1) {
      block_height = laid_out_lines_[0].size.height;
    } else {
      block_height = (laid_out_lines_.size() - 1) * line_height +
                     (font_metrics.fDescent - font_metrics.fAscent);
    }
  }

  return {.width = CalculateMeasuredLength(width_mode, width, block_width),
          .height = CalculateMeasuredLength(height_mode, height, block_height)};
}

void Label::CalculateTextAlignmentOffsetsIfNeeded() {}

void Label::AssignDefaultFontIfUnassigned() {
  if (font_ == nullptr) font_ = GetBook12UiFont();
}

}  // namespace components
}  // namespace ui
}  // namespace perception
