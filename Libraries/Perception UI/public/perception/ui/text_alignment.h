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

#pragma once

#include <string_view>

#include "types.h"

class SkFont;

namespace perception {

namespace ui {

enum class TextAlignment {
  TopLeft = 0,
  TopCenter = 1,
  TopRight = 2,
  MiddleLeft = 3,
  MiddleCenter = 4,
  MiddleRight = 5,
  BottomLeft = 6,
  BottomCenter = 7,
  BottomRight = 8
};

void CalculateTextAlignment(std::string_view text, float width, float height,
                            TextAlignment alignment, SkFont& font, float& x,
                            float& y);

void CalculateAlignment(float item_width, float item_height,
                        float container_width, float container_height,
                        TextAlignment alignment, float& x, float& y);

}  // namespace ui
}  // namespace perception
