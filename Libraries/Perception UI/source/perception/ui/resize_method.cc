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

#include "perception/ui/resize_method.h"

#include <algorithm>

#include "perception/ui/size.h"

namespace perception {
namespace ui {

Size CalculateResize(ResizeMethod method, const Size& item_size,
                     const Size& container_size, float display_scale) {
  switch (method) {
    case ResizeMethod::Original:
      return {.width = item_size.width * display_scale,
              .height = item_size.height * display_scale};
    case ResizeMethod::PixelPerfect:
      return item_size;
    case ResizeMethod::Stretch:
      return container_size;
    case ResizeMethod::Cover:
    case ResizeMethod::Contain: {
      if (item_size.width == 0.0f || item_size.height == 0.0f) {
        return {.width = 0.0f, .height = 0.0f};
      }

      float width_ratio = container_size.width / item_size.width;
      float height_ratio = container_size.height / item_size.height;

      float scale_amount = method == ResizeMethod::Cover
                               ? std::max(width_ratio, height_ratio)
                               : std::min(width_ratio, height_ratio);

      return {.width = item_size.width * scale_amount,
              .height = item_size.height * scale_amount};
    }
  }
}

}  // namespace ui
}  // namespace perception
