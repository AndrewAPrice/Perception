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
#include "perception/ui/components/block.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "perception/draw.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {
// shadows:
// https://skia.googlesource.com/skia/+/2e551697dc56/modules/skottie/src/effects/ShadowStyles.cpp?autodive=0%2F%2F%2F%2F%2F%2F%2F%2F%2F%2F

Block::Block() : needs_draw_(false), needs_draw_post_children_(false) {}

void Block::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  if (needs_draw_) strong_node->OnDraw(std::bind_front(&Block::Draw, this));

  if (needs_draw_post_children_)
    strong_node->OnDrawPostChildren(
        std::bind_front(&Block::DrawPostChildren, this));

  if (needs_hit_test_)
    node_.lock()->SetHitTestFunction(std::bind_front(&Block::HitTest, this));
}

void Block::SetBorderColor(uint32 color) {
  if (border_color_ == color) return;
  border_color_ = color;
  if (!node_.expired()) node_.lock()->Invalidate();
}

uint32 Block::GetBorderColor() const { return border_color_; }

void Block::SetBorderWidth(float width) {
  if (border_width_ == width) return;
  border_width_ = width;
  SetNeedsDrawPostChildren();
  if (!node_.expired()) node_.lock()->Invalidate();
}

double Block::GetBorderWidth() const { return border_width_; }

void Block::SetBorderRadius(float radius) {
  if (border_radius_ == radius) return;
  border_radius_ = radius;
  SetNeedsHitTest();
  if (!node_.expired()) node_.lock()->Invalidate();
}

double Block::GetBorderRadius() const { return border_radius_; }

void Block::SetFillColor(uint32 color) {
  if (fill_color_ == color) return;
  fill_color_ = color;
  SetNeedsDraw();
  if (!node_.expired()) node_.lock()->Invalidate();
}

uint32 Block::GetFillColor() const { return fill_color_; }

void Block::SetClipContents(bool clip_contents) {
  if (clip_contents_ == clip_contents) return;
  clip_contents_ = clip_contents;
  SetNeedsDraw();
  SetNeedsDrawPostChildren();
  if (!node_.expired()) {
    auto node = node_.lock();
    node->GetLayout().SetOverflow(clip_contents ? YGOverflowHidden
                                                : YGOverflowVisible);
    node->Invalidate();
  }
}

bool Block::GetClipContents() const { return clip_contents_; }

void Block::SetImageEffect(std::shared_ptr<ImageEffect> image_effect) {
  if (image_effect_ == image_effect) return;
  image_effect_ = image_effect;
  if (!node_.expired()) node_.lock()->Invalidate();
}

std::shared_ptr<ImageEffect> Block::GetImageEffect() const {
  return image_effect_;
}

void Block::Draw(const DrawContext& draw_context) {
  const float& x = draw_context.area.origin.x;
  const float& y = draw_context.area.origin.y;
  const float& width = draw_context.area.size.width;
  const float& height = draw_context.area.size.height;

  if (clip_contents_) {
    draw_context.skia_canvas->save();
    SkPath path;
    path.addRoundRect({x, y, x + width, y + height}, border_radius_,
                      border_radius_);
    draw_context.skia_canvas->clipPath(path, SkClipOp::kIntersect, true);
  }

  if (fill_color_) {
    SkPaint p;
    p.setAntiAlias(true);
    p.setColor(fill_color_);
    p.setStyle(SkPaint::kFill_Style);
    if (image_effect_) p.setImageFilter(image_effect_->GetSkiaImageFilter());

    if (!clip_contents_ && border_radius_ > 0) {
      draw_context.skia_canvas->drawRoundRect(
          {x, y, x + width, y + height}, border_radius_, border_radius_, p);
    } else {
      draw_context.skia_canvas->drawRect({x, y, x + width, y + height}, p);
    }
  }
}

void Block::DrawPostChildren(const DrawContext& draw_context) {
  if (clip_contents_) draw_context.skia_canvas->restore();
  if (border_color_ && border_width_ > 0) {
    SkPaint p;
    if (image_effect_) p.setImageFilter(image_effect_->GetSkiaImageFilter());

    const float& x = draw_context.area.origin.x;
    const float& y = draw_context.area.origin.y;
    const float& width = draw_context.area.size.width;
    const float& height = draw_context.area.size.height;

    // Draw the outline.
    p.setAntiAlias(true);
    p.setColor(border_color_);
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(border_width_);
    draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                            border_radius_, border_radius_, p);
  }
}

bool Block::HitTest(const Point& point, const Size& size) {
  if (border_radius_ == 0.0f) return true;

  Point p;
  if (HitTestAlongDimension(point.x, size.width, p.x) ||
      HitTestAlongDimension(point.y, size.height, p.y)) {
    return true;
  }

  return (p.x * p.x + p.y * p.y) <= (border_radius_ * border_radius_);
}

bool Block::HitTestAlongDimension(float value, float length, float& relative) {
  if (value > border_radius_) {
    if (value < length - border_radius_) {
      // Falls within non-border curve.
      return true;
    }
    relative = value - length + border_radius_;
  } else {
    relative = value - border_radius_;
  }
  return false;
}

void Block::SetNeedsDraw() {
  if (needs_draw_) return;
  needs_draw_ = true;

  if (!node_.expired())
    node_.lock()->OnDraw(std::bind_front(&Block::Draw, this));
}

void Block::SetNeedsDrawPostChildren() {
  if (needs_draw_post_children_) return;
  needs_draw_post_children_ = true;

  if (!node_.expired())
    node_.lock()->OnDrawPostChildren(
        std::bind_front(&Block::DrawPostChildren, this));
}

void Block::SetNeedsHitTest() {
  if (needs_hit_test_) return;
  needs_hit_test_ = true;

  if (!node_.expired())
    node_.lock()->SetHitTestFunction(std::bind_front(&Block::HitTest, this));
}

}  // namespace components
}  // namespace ui
}  // namespace perception
