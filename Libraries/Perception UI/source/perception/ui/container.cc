// Copyright 2022 Google LLC
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

#include "perception/ui/container.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "perception/draw.h"
#include "perception/font.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

  // shadows: https://skia.googlesource.com/skia/+/2e551697dc56/modules/skottie/src/effects/ShadowStyles.cpp?autodive=0%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F

std::shared_ptr<Container> Container::Create() {
  std::shared_ptr<Container> container(new Container());
  container->SetPadding(YGEdgeAll, kContainerPadding);
  container->SetBorderColor(kContainerBorderColor);
  container->SetBorderRadius(kContainerBorderRadius);
  container->SetBorderWidth(kContainerBorderWidth);
  container->SetBorderColor(kContainerBorderColor);
  return container;
}

Container::~Container() {}

Container* Container::SetBorderColor(uint32 color) {
  if (border_color_ == color) return this;
  border_color_ = color;
  InvalidateRender();
  return this;
}

Container* Container::SetBorderWidth(float width) {
  if (border_width_ == width) return this;
  border_width_ = width;
  InvalidateRender();
  return this;
}

Container* Container::SetBorderRadius(float radius) {
  if (border_radius_ == radius) return this;
  border_radius_ = radius;
  InvalidateRender();
  return this;
}

Container* Container::SetBackgroundColor(uint32 color) {
  if (background_color_ == color) return this;
  background_color_ = color;
  InvalidateRender();
  return this;
}

Container* Container::SetClipContents(bool clip_contents) {
  if (clip_contents_ == clip_contents) return this;
  clip_contents_ = clip_contents;
  InvalidateRender();
  return this;
}

uint32 Container::GetBorderColor() const { return border_color_; }

double Container::GetBorderWidth() const { return border_width_; }

double Container::GetBorderRadius() const { return border_radius_; }

uint32 Container::GetBackgroundColor() const { return background_color_; }

bool Container::GetClipContents() const { return clip_contents_; }

Container::Container() : clip_contents_(false) {
  SetMargin(YGEdgeAll, kMarginAroundWidgets);
}

void Container::Draw(DrawContext& draw_context) {
  // draw_context.skia_canvas->save();
  SkPaint p;
  p.setAntiAlias(true);

  if (image_effect_) p.setImageFilter(image_effect_->GetSkiaImageFilter());

  float x = GetLeft() + draw_context.offset_x;
  float y = GetTop() + draw_context.offset_y;
  float width = GetCalculatedWidth();
  float height = GetCalculatedHeight();

  if (background_color_) {
    p.setColor(background_color_);
    p.setStyle(SkPaint::kFill_Style);
    draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                            border_radius_, border_radius_, p);
  }

  if (clip_contents_) {
    draw_context.skia_canvas->save();
    SkPath path;
    path.addRoundRect({x, y, x + width, y + height}, border_radius_,
                      border_radius_);
    draw_context.skia_canvas->clipPath(path, SkClipOp::kIntersect, true);
  }

  // Draw the contents of the container.
  DrawContents(draw_context);

  if (clip_contents_) draw_context.skia_canvas->restore();

  if (border_color_ && border_width_ > 0) {
    // Draw the outline.
    p.setColor(border_color_);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(border_width_);
    draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                            border_radius_, border_radius_, p);
  }

  //  draw_context.skia_canvas->restore();
}

void Container::DrawContents(DrawContext& draw_context) {
  // Draw the contents of the container.
  Widget::Draw(draw_context);
}

}  // namespace ui
}  // namespace perception