// Copyright 2023 Google LLC
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

struct Size;

enum class ResizeMethod {
  // Show the item in the original size. (This size can scale with the UI.)
  Original = 0,

  // Show the item in the original, pixel perfect, size.
  PixelPerfect = 1,

  // Stretch the item to fit the container perfectly, not preserving aspect
  // ratio.
  Stretch = 2,

  // Resize the item to fully cover the container while preserving the aspect
  // ratio.
  Cover = 3,

  // Resize the item to it's fully contained within the container while
  // preserving the aspect ratio.
  Contain = 4
};

Size CalculateResize(ResizeMethod method, const Size& item_size,
                     const Size& display_size, float display_scale);

}  // namespace ui
}  // namespace perception
