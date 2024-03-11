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

namespace perception {
namespace ui {

void CalculateResize(ResizeMethod method, float item_width, float item_height,
                     float container_width, float container_height,
                     float display_scale, float &output_width,
                     float &output_height) {
  switch (method) {
    case ResizeMethod::Original:
      output_width = item_width * display_scale;
      output_height = item_height * display_scale;
      break;
    case ResizeMethod::PixelPerfect:
      output_width = item_width;
      output_height = item_height;
      break;
    case ResizeMethod::Stretch:
      output_width = container_width;
      output_height = container_height;
      break;
    case ResizeMethod::Cover:
    case ResizeMethod::Contain: {
      if (item_height == 0.0f || item_width == 0.0f) {
        output_width = output_height = 0.0f;
        break;
      }

      float width_ratio = container_width / item_width;
      float height_ratio = container_height / item_height;

      float scale_amount = method == ResizeMethod::Cover
                               ? std::max(width_ratio, height_ratio)
                               : std::min(width_ratio, height_ratio);

      output_width = item_width * scale_amount;
      output_height = item_height * scale_amount;
      break;
    }
  }
}

}  // namespace ui
}  // namespace perception
