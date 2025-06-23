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

#include "perception/ui/components/image_view.h"

#include <iostream>
#include <memory>

#include "include/core/SkCanvas.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/image.h"

namespace perception {
namespace ui {
namespace components {
namespace {

float CalculateMeasuredLength(YGMeasureMode mode, float requested_length,
                              float calculated_length) {
  if (mode == YGMeasureModeExactly) return requested_length;
  if (mode == YGMeasureModeAtMost)
    return std::min(requested_length, calculated_length);
  return calculated_length;
}

}  // namespace

ImageView::ImageView()
    : alignment_(TextAlignment::MiddleCenter),
      resize_method_(ResizeMethod::Contain),
      needs_realignment_(true) {}

void ImageView::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node.expired()) return;
  auto strong_node = node.lock();
  strong_node->OnDraw(std::bind_front(&ImageView::Draw, this));
  strong_node->SetMeasureFunction(std::bind_front(&ImageView::Measure, this));
}

void ImageView::SetImage(std::shared_ptr<Image> image) {
  if (image_ == image) return;
  image_ = image;
  needs_realignment_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

std::shared_ptr<Image> ImageView::GetImage() { return image_; }

void ImageView::SetAlignment(TextAlignment alignment) {
  if (alignment == alignment_) return;
  alignment_ = alignment;
  needs_realignment_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

TextAlignment ImageView::GetAlignment() const { return alignment_; }

void ImageView::SetResizeMethod(ResizeMethod method) {
  if (resize_method_ == method) return;
  resize_method_ = method;
  needs_realignment_ = true;
  if (!node_.expired()) {
    auto node = node_.lock();
    node->Remeasure();
    node->Invalidate();
  }
}

ResizeMethod ImageView::GetResizeMethod() const { return resize_method_; }

void ImageView::Draw(const DrawContext& draw_context) {
  if (draw_context.area.size != node_size_) {
    node_size_ = draw_context.area.size;
    needs_realignment_ = true;
  }
  CalculateAlignmentOffsetsIfNeeded();
  bool image_matches_displayed_dimensions;
  SkImage* image =
      image_->GetSkImage(display_size_, image_matches_displayed_dimensions);

  bool clip_contents = !image || display_size_.width > node_size_.width ||
                       display_size_.height > node_size_.height;

  if (clip_contents) {
    draw_context.skia_canvas->save();
    /* SkPath path;
     path.addRect(SkRect::MakeXYWH(x, y, width, height));
     draw_context.skia_canvas->clipPath(path, SkClipOp::kIntersect, true);*/
  }

  if (image) {
    // This is a raster image.
    SkPaint paint;
    paint.setAntiAlias(true);

    Point position = draw_context.area.origin + position_;
    if (image_matches_displayed_dimensions) {
      draw_context.skia_canvas->drawImage(image, position.x, position.y);
    } else {
      draw_context.skia_canvas->drawImageRect(
          image,
          SkRect::MakeXYWH(position.x, position.y, display_size_.width,
                           display_size_.height),
          SkSamplingOptions(SkFilterMode::kLinear), &paint);
    }
  } else if (SkSVGDOM* svg = image_->GetSkSVGDOM(display_size_)) {
    // This is an SVG image.
    draw_context.skia_canvas->translate(position_.x, position_.y);
    draw_context.skia_canvas->scale(display_size_.width / image_size_.width,
                                    display_size_.height / image_size_.height);
    svg->render(draw_context.skia_canvas);
  } else {
    std::cout << "Not sure how to draw the provided image." << std::endl;
  }

  if (clip_contents) draw_context.skia_canvas->restore();
}

Size ImageView::Measure(float width, YGMeasureMode width_mode, float height,
                        YGMeasureMode height_mode) {
  Size image_size = GetImageSize({.width = width, .height = height});
  return {.width = CalculateMeasuredLength(width_mode, width, image_size.width),
          .height =
              CalculateMeasuredLength(height_mode, height, image_size.height)};
}

void ImageView::CalculateAlignmentOffsetsIfNeeded() {
  if (!needs_realignment_) return;

  image_size_ = GetImageSize(display_size_);
  display_size_ =
      CalculateResize(resize_method_, image_size_, node_size_, 1.0f);
  position_ = CalculateAlignment(display_size_, node_size_, alignment_);

  needs_realignment_ = false;
}

Size ImageView::GetImageSize(const Size& container_size) {
  if (image_) return image_->GetSize(container_size);
}

}  // namespace components
}  // namespace ui
}  // namespace perception