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

struct Point;
struct Size;

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

Point CalculateTextAlignment(std::string_view text, const Size& container_size,
                             TextAlignment alignment, SkFont& font);

Point CalculateAlignment(const Size& item_size, const Size& container_size,
                         TextAlignment alignment);

}  // namespace ui
}  // namespace perception
