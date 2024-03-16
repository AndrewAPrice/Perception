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

#pragma once

class SkFont;

#include <memory>
#include <string>

#include "include/core/SkImageFilter.h"

namespace perception {
namespace ui {

// An image effect that can be applied to a widget.
class ImageEffect {
 public:
  ImageEffect(std::string key, sk_sp<SkImageFilter> skia_image_filter);
  ~ImageEffect();

  sk_sp<SkImageFilter> GetSkiaImageFilter() { return skia_image_filter_; }

  // Returns a drop shadow effect.
  static std::shared_ptr<ImageEffect> DropShadow(uint32_t color, float opacity,
                                                 float angle, float size,
                                                 float distance);

  // Returns an inner shadow effect.
  static std::shared_ptr<ImageEffect> InnerShadow(uint32_t color, float opacity,
                                                  float angle, float size,
                                                  float distance);

  // Returns an inner shadow effect but doesn't draw the contents.
  static std::shared_ptr<ImageEffect> InnerShadowOnly(uint32_t color, float opacity,
                                                  float angle, float size,
                                                  float distance);

 public:
  std::string key_;
  sk_sp<SkImageFilter> skia_image_filter_;
};

}  // namespace ui
}  // namespace perception
