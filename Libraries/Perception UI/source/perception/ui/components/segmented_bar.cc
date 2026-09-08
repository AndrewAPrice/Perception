// Copyright 2026 Google LLC
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

#include "perception/ui/components/segmented_bar.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "perception/ui/components/tooltip.h"
#include "perception/ui/layout.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::SegmentedBar>;

namespace ui {
namespace components {

SegmentedBar::SegmentedBar() = default;

SegmentedBar::~SegmentedBar() {
  if (tooltip_)
    tooltip_->HideTooltip();
}

void SegmentedBar::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  auto strong_node = node.lock();
  if (!strong_node)
    return;

  tooltip_ = strong_node->GetOrAdd<Tooltip>();

  std::weak_ptr<SegmentedBar> weak_self = weak_from_this();
  strong_node->OnDraw([weak_self](const DrawContext& context) {
    if (auto self = weak_self.lock())
      self->Draw(context);
  });

  strong_node->OnMouseHover([weak_self](const Point& point) {
    if (auto self = weak_self.lock())
      self->HandleMouseHover(point);
  });

  strong_node->OnMouseLeave([weak_self]() {
    if (auto self = weak_self.lock())
      self->HandleMouseLeave();
  });
}

void SegmentedBar::SetSegments(const std::vector<SegmentedBarSegment>& segments) {
  segments_ = segments;
  last_hovered_segment_index_ = -1;
  if (tooltip_) {
    tooltip_->SetText("");
    tooltip_->HideTooltip();
  }
  auto strong_node = node_.lock();
  if (strong_node)
    strong_node->Invalidate();
}

int SegmentedBar::GetSegmentIndexAt(float x, float width) const {
  if (width <= 0.0f || !std::isfinite(width) || x < 0.0f || x > width)
    return -1;

  float current_x = 0.0f;
  for (size_t i = 0; i < segments_.size(); ++i) {
    const auto& seg = segments_[i];
    if (seg.ratio <= 0.0f || !std::isfinite(seg.ratio))
      continue;
    float seg_width = seg.ratio * width;
    if (seg_width <= 0.0f || !std::isfinite(seg_width))
      continue;
    if (current_x + seg_width > width)
      seg_width = std::max(0.0f, width - current_x);
    if (seg_width <= 0.0f)
      continue;

    if (x >= current_x && (x < current_x + seg_width || (i + 1 == segments_.size() && x <= current_x + seg_width)))
      return static_cast<int>(i);

    current_x += seg_width;
  }
  return -1;
}

std::string SegmentedBar::FormatTooltipText(const SegmentedBarSegment& segment) const {
  int pct = static_cast<int>(std::round(segment.ratio * 100.0f));
  if (segment.label.empty())
    return std::to_string(pct) + "%";

  return segment.label + " (" + std::to_string(pct) + "%)";
}

void SegmentedBar::HandleMouseHover(const Point& point) {
  if (!tooltip_)
    return;

  auto strong_node = node_.lock();
  if (!strong_node)
    return;

  float width = strong_node->GetSize().width;
  int index = GetSegmentIndexAt(point.x, width);

  if (index == -1) {
    if (last_hovered_segment_index_ != -1) {
      last_hovered_segment_index_ = -1;
      tooltip_->SetText("");
    }
    return;
  }

  if (index != last_hovered_segment_index_) {
    last_hovered_segment_index_ = index;
    std::string text = FormatTooltipText(segments_[index]);
    tooltip_->SetText(text);
    tooltip_->ShowTooltipAt(point);
  }
}

void SegmentedBar::HandleMouseLeave() {
  last_hovered_segment_index_ = -1;
  if (tooltip_) {
    tooltip_->SetText("");
    tooltip_->HideTooltip();
  }
}

void SegmentedBar::Draw(const DrawContext& context) {
  SkCanvas* canvas = context.skia_canvas;
  if (!canvas)
    return;

  float width = context.area.size.width;
  float height = context.area.size.height;
  if (width <= 0.0f || height <= 0.0f || !std::isfinite(width) || !std::isfinite(height))
    return;

  float x0 = context.area.origin.x;
  float y0 = context.area.origin.y;
  if (!std::isfinite(x0) || !std::isfinite(y0))
    return;

  SkRect bounds = SkRect::MakeXYWH(x0, y0, width, height);
  if (!bounds.isSorted())
    return;

  SkRRect clip_rrect =
      SkRRect::MakeRectXY(bounds, kSegmentedBarBorderRadius, kSegmentedBarBorderRadius);

  canvas->save();
  canvas->clipRRect(clip_rrect, true);

  SkPaint bg_paint;
  bg_paint.setColor(kSegmentedBarBackgroundColor);
  bg_paint.setAntiAlias(true);
  canvas->drawRect(bounds, bg_paint);

  float current_x = x0;
  for (const auto& seg : segments_) {
    if (seg.ratio <= 0.0f || !std::isfinite(seg.ratio))
      continue;
    float seg_width = seg.ratio * width;
    if (seg_width <= 0.0f || !std::isfinite(seg_width))
      continue;
    if (current_x + seg_width > x0 + width)
      seg_width = std::max(0.0f, (x0 + width) - current_x);
    if (seg_width <= 0.0f)
      continue;

    SkPaint seg_paint;
    seg_paint.setColor(seg.color);
    seg_paint.setAntiAlias(true);

    SkRect seg_rect = SkRect::MakeXYWH(current_x, y0, seg_width, height);
    if (seg_rect.isSorted())
      canvas->drawRect(seg_rect, seg_paint);
    current_x += seg_width;
  }

  canvas->restore();

  SkPaint border_paint;
  border_paint.setStyle(SkPaint::kStroke_Style);
  border_paint.setColor(kSegmentedBarBorderColor);
  border_paint.setStrokeWidth(1.0f);
  border_paint.setAntiAlias(true);
  canvas->drawRRect(clip_rrect, border_paint);
}

}  // namespace components
}  // namespace ui
}  // namespace perception
