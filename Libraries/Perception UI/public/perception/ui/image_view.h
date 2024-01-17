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

#include <functional>
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/image.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class ImageView : public Widget {
 public:
  ImageView();
  virtual ~ImageView();

  ImageView* SetImage(std::shared_ptr<Image> image);
  std::shared_ptr<Image> GetImage();
  ImageView* SetAlignment(TextAlignment alignment);
  ImageView* SetResizeMethod(ResizeMethod method);

 private:
  virtual void Draw(DrawContext& draw_context) override;

  static YGSize Measure(const YGNode* node, float width,
                        YGMeasureMode width_mode, float height,
                        YGMeasureMode height_mode);

  static void LayoutDirtied(const YGNode* node);

  std::shared_ptr<Image> image_;
  TextAlignment alignment_;
  ResizeMethod resize_method_;
  bool realign_image_;
  float image_x_, image_y_;
  float image_width_, image_height_;
  float displayed_width_, displayed_height_;
};

}  // namespace ui
}  // namespace perception
