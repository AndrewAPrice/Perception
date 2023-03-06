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

#include "perception/ui/image_view.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "perception/draw.h"
#include "perception/ui/button.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

ImageView::ImageView()
    : alignment_(TextAlignment::TopLeft),
      resize_method_(ResizeMethod::Original),
      realign_image_(true) {
  YGNodeSetMeasureFunc(yoga_node_, &ImageView::Measure);
  YGNodeSetDirtiedFunc(yoga_node_, &ImageView::LayoutDirtied);
  SetMargin(YGEdgeAll, kMarginAroundWidgets);
}

ImageView::~ImageView() {}

ImageView* ImageView::SetImage(std::shared_ptr<Image> image) {
  if (image_ == image) return this;

  image_ = image;

  YGNodeMarkDirty(yoga_node_);
  InvalidateRender();
  realign_image_ = true;
  return this;
}

std::shared_ptr<Image> ImageView::GetImage() { return image_; }

ImageView* ImageView::SetAlignment(TextAlignment alignment) {
  if (alignment_ == alignment) return this;

  alignment_ = alignment;
  realign_image_ = true;
  InvalidateRender();
  return this;
}

ImageView* ImageView::SetResizeMethod(ResizeMethod method) {
  if (resize_method_ == method) return this;

  if (resize_method_ == ResizeMethod::PixelPerfect ||
      method == ResizeMethod::PixelPerfect) {
    // Switching to or from pixel perfect resizing invalidates the size of the
    // image, which can cause the layout to change.
    YGNodeMarkDirty(yoga_node_);
  }

  resize_method_ = method;
  realign_image_ = true;
  InvalidateRender();
  return this;
}

void ImageView::Draw(DrawContext& draw_context) {
  if (!image_) return;

  float width = GetCalculatedWidth();
  float height = GetCalculatedHeight();

  if (realign_image_) {
    image_->GetSize(width, height, image_width_, image_height_);
    CalculateResize(resize_method_, image_width_, image_height_, width, height,
                    1.0f, displayed_width_, displayed_height_);

    CalculateAlignment(displayed_width_, displayed_height_, width, height,
                       alignment_, image_x_, image_y_);

    realign_image_ = false;
  }

  if (image_width_ < 0.01f || image_height_ <= 0.01f) {
    return;
  }

  float x = GetLeft() + draw_context.offset_x;
  float y = GetTop() + draw_context.offset_y;

  bool image_matches_displayed_dimensions;
  SkImage* image = image_->GetSkImage(displayed_width_, displayed_height_,
                                      image_matches_displayed_dimensions);

  bool clip_contents =
      !image || displayed_width_ > width || displayed_height_ > height;

  if (clip_contents) {
    draw_context.skia_canvas->save();
    /* SkPath path;
     path.addRect(SkRect::MakeXYWH(x, y, width, height));
     draw_context.skia_canvas->clipPath(path, SkClipOp::kIntersect, true);*/
  }

  if (image) {
    // This is a raster iamge.
    SkPaint paint;
    paint.setAntiAlias(true);

    x += image_x_;
    y += image_y_;

    std::cout << "Image is: " << image_width_ << "," << image_height_ << std::endl;
    std::cout << "Container is: " << width << "," << height << std::endl;
    std::cout << "Display size is: " << displayed_width_ << "," << displayed_height_ << std::endl;
    std::cout << "Position is: " << image_x_ << "," << image_y_ << std::endl;

    if (image_matches_displayed_dimensions) {
      draw_context.skia_canvas->drawImage(image, x, y);
    } else {
      draw_context.skia_canvas->drawImageRect(
          image, SkRect::MakeXYWH(x, y, displayed_width_, displayed_height_),
          SkSamplingOptions(SkFilterMode::kLinear), &paint);
    }
    std::cout << "done drawing" << std::endl;
  } else if (SkSVGDOM* svg =
                 image_->GetSkSVGDOM(displayed_width_, displayed_height_)) {
    // This is an SVG image.
    draw_context.skia_canvas->translate(image_x_, image_y_);
    draw_context.skia_canvas->scale(displayed_width_ / image_width_,
                                    displayed_height_ / image_height_);
    svg->render(draw_context.skia_canvas);
  } else {
    std::cout << "Not sure how to draw the provided image." << std::endl;
  }

  if (clip_contents) draw_context.skia_canvas->restore();
}

YGSize ImageView::Measure(YGNodeRef node, float width, YGMeasureMode width_mode,
                          float height, YGMeasureMode height_mode) {
  ImageView* image_view = (ImageView*)YGNodeGetContext(node);
  YGSize size;

  float image_width = 0;
  float image_height = 0;
  if (image_view->image_)
    image_view->image_->GetSize(width, height, image_width, image_height);

  if (width_mode == YGMeasureModeExactly) {
    size.width = width;
  } else {
    size.width = image_width;
    if (width_mode == YGMeasureModeAtMost) {
      size.width = std::min(width, size.width);
    }
  }
  if (height_mode == YGMeasureModeExactly) {
    size.height = height;
  } else {
    size.height = image_height;
    if (height_mode == YGMeasureModeAtMost) {
      size.height = std::min(height, size.height);
    }
  }

  image_view->realign_image_ = true;
  return size;
}

void ImageView::LayoutDirtied(YGNode* node) {
  ImageView* image_view = (ImageView*)YGNodeGetContext(node);
  image_view->realign_image_ = true;
}

}  // namespace ui
}  // namespace perception