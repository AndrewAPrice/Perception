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
#include "include/core/SkRect.h"

namespace perception {
namespace ui {

void CalculateTextAlignment(std::string_view text, int width, int height,
                            TextAlignment alignment, SkFont& font, int& x,
                            int& y) {
  SkRect bounds;
  (void)font.measureText(&text[0], text.length(), SkTextEncoding::kUTF8, &bounds);

  switch (alignment) {
    case TextAlignment::TopLeft:
    case TextAlignment::TopCenter:
    case TextAlignment::TopRight:
      y = 0;
      break;
    case TextAlignment::MiddleLeft:
    case TextAlignment::MiddleCenter:
    case TextAlignment::MiddleRight:
      y = height / 2 - bounds.height() / 2;
      break;
    case TextAlignment::BottomLeft:
    case TextAlignment::BottomCenter:
    case TextAlignment::BottomRight:
      y = height - bounds.height();
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
      x = width / 2 - bounds.width() / 2;
      break;
    case TextAlignment::TopRight:
    case TextAlignment::MiddleRight:
    case TextAlignment::BottomRight:
      x = width - bounds.width();
      break;
  }
}

}  // namespace ui
}  // namespace perception