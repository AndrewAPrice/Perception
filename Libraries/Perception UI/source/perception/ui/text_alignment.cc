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

#include "perception/ui/text_alignment.h"

#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkRect.h"

namespace perception {
namespace ui {

void CalculateTextAlignment(std::string_view text, float width, float height,
                            TextAlignment alignment, SkFont& font, float& x,
                            float& y) {
  SkRect bounds;
  (void)font.measureText(&text[0], text.length(), SkTextEncoding::kUTF8,
                         &bounds);

  SkFontMetrics font_metrics;
  float line_height = font.getMetrics(&font_metrics);

  CalculateAlignment(bounds.width(), font_metrics.fDescent - font_metrics.fAscent,
    width, height, alignment, x, y);

  // Skia draws text from the baseline.
  y -= font_metrics.fAscent;
}

void CalculateAlignment(float item_width, float item_height,
  float container_width, float container_height,
  TextAlignment alignment,
  float& x, float& y) {
  switch (alignment) {
    case TextAlignment::TopLeft:
    case TextAlignment::TopCenter:
    case TextAlignment::TopRight:
      y = 0;
      break;
    case TextAlignment::MiddleLeft:
    case TextAlignment::MiddleCenter:
    case TextAlignment::MiddleRight:
      y = container_height / 2.0f - item_height / 2.0f;
      break;
    case TextAlignment::BottomLeft:
    case TextAlignment::BottomCenter:
    case TextAlignment::BottomRight:
      y = item_height - container_height;
      break;
  }

  switch (alignment) {
    case TextAlignment::TopLeft:
    case TextAlignment::MiddleLeft:
    case TextAlignment::BottomLeft:
      x = 0;
      break;
    case TextAlignment::TopCenter:
    case TextAlignment::MiddleCenter:
    case TextAlignment::BottomCenter:
      x = container_width / 2.0f - item_width / 2.0f;
      break;
    case TextAlignment::TopRight:
    case TextAlignment::MiddleRight:
    case TextAlignment::BottomRight:
      x = container_width - item_width;
      break;
  }

}

}  // namespace ui
}  // namespace perception