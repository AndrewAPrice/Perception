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
#include "include/core/SkFontTypes.h"
#include "include/core/SkRect.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"

namespace perception {
namespace ui {

Point CalculateTextAlignment(std::string_view text, const Size& container_size,
                             TextAlignment alignment, SkFont& font) {
  SkRect bounds;
  (void)font.measureText(&text[0], text.length(), SkTextEncoding::kUTF8,
                         &bounds);

  SkFontMetrics font_metrics;
  float line_height = font.getMetrics(&font_metrics);

  Size item_size = {.width = bounds.width(),
                    .height = font_metrics.fDescent - font_metrics.fAscent};

  Point point = CalculateAlignment(item_size, container_size, alignment);

  // Skia draws text from the baseline.
  point.y -= font_metrics.fAscent;
  return point;
}

Point CalculateAlignment(const Size& item_size, const Size& container_size,
                         TextAlignment alignment) {
  Point position;
  switch (alignment) {
    case TextAlignment::TopLeft:
    case TextAlignment::TopCenter:
    case TextAlignment::TopRight:
      position.y = 0;
      break;
    case TextAlignment::MiddleLeft:
    case TextAlignment::MiddleCenter:
    case TextAlignment::MiddleRight:
      position.y = container_size.height / 2.0f - item_size.height / 2.0f;
      break;
    case TextAlignment::BottomLeft:
    case TextAlignment::BottomCenter:
    case TextAlignment::BottomRight:
      position.y = container_size.height - item_size.height;
      break;
  }

  switch (alignment) {
    case TextAlignment::TopLeft:
    case TextAlignment::MiddleLeft:
    case TextAlignment::BottomLeft:
      position.x = 0;
      break;
    case TextAlignment::TopCenter:
    case TextAlignment::MiddleCenter:
    case TextAlignment::BottomCenter:
      position.x = container_size.width / 2.0f - item_size.width / 2.0f;
      break;
    case TextAlignment::TopRight:
    case TextAlignment::MiddleRight:
    case TextAlignment::BottomRight:
      position.x = container_size.width - item_size.width;
      break;
  }
  return position;
}

}  // namespace ui
}  // namespace perception