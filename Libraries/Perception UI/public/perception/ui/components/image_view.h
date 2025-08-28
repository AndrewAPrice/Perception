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

#include <memory>
#include <string>

#include "perception/ui/image.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/resize_method.h"
#include "perception/ui/size.h"
#include "perception/ui/text_alignment.h"
#include "yoga/Yoga.h"
#include "perception/type_id.h"

namespace perception {
namespace ui {

struct DrawContext;

namespace components {


// An image displays an image.
class ImageView : public UniqueIdentifiableType<ImageView> {
 public:
  // Creates a basic ImageView that displays an image.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicImage(std::shared_ptr<Image> image,
                                          Modifiers... modifiers) {
    return Node::Empty(
        [image](ImageView& image_view) { image_view.SetImage(image); },
        modifiers...);
  }

  ImageView();

  void SetNode(std::weak_ptr<Node> node);

  void SetImage(std::shared_ptr<Image> image);
  std::shared_ptr<Image> GetImage();

  void SetAlignment(TextAlignment alignment);
  TextAlignment GetAlignment() const;

  void SetResizeMethod(ResizeMethod method);
  ResizeMethod GetResizeMethod() const;

 private:
  std::shared_ptr<Image> image_;
  TextAlignment alignment_;
  ResizeMethod resize_method_;
  bool needs_realignment_;
  std::weak_ptr<Node> node_;
  Point position_;
  Size node_size_;
  Size image_size_;
  Size display_size_;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);
  void CalculateAlignmentOffsetsIfNeeded();
  Size GetImageSize(const Size& container_size);
};

}  // namespace components
}  // namespace ui

  
extern template class UniqueIdentifiableType<ui::components::ImageView>;

}  // namespace perception