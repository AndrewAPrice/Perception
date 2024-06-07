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

#include "perception/ui/scroll_container.h"

#include <iostream>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/image_effect.h"
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {
namespace {

// Default size of the contents if for some reason the scroll container's size
// is set to 'auto'.
constexpr float kDefaultSize = 100.0f;

}  // namespace

std::shared_ptr<ScrollContainer> ScrollContainer::Create(
    std::shared_ptr<ParentlessWidget> content, bool show_vertical_scroll_bar,
    bool show_horizontal_scroll_bar) {
  std::shared_ptr<ScrollContainer> scroll_container(new ScrollContainer(
      content, show_vertical_scroll_bar, show_horizontal_scroll_bar));

  scroll_container->SetOverlayImageEffect(
      ImageEffect::InnerShadowOnly(0xFF000000, 0.25f, 0, 2.0f, 0));

  std::weak_ptr<ScrollContainer> weak_container = scroll_container;
  content->InvalidateParentRenderHandler([weak_container]() {
    if (auto container = weak_container.lock()) container->InvalidateRender();
  });
  content->InvalidateParentLayoutHandler([weak_container]() {
    if (auto container = weak_container.lock())
      container->CheckIfContentsChangedSize();
  });

  scroll_container->RebuildLayout();

  return scroll_container;
}

ScrollContainer::~ScrollContainer() {}

bool ScrollContainer::GetWidgetAt(float x, float y,
                                  std::shared_ptr<Widget>& widget,
                                  float& x_in_selected_widget,
                                  float& y_in_selected_widget) {
  float left = GetLeft();
  float top = GetTop();
  float right = left + GetCalculatedWidth();
  float bottom = top + GetCalculatedHeight();

  if (x < left || x >= right || y < top || y >= bottom)
    return false;  // Outside of our bounds.

  x -= left;
  y -= top;

  MaybeUpdateScrollBars();

  // Check the scroll bars first.
  if (horizontal_scroll_bar_) {
    if (horizontal_scroll_bar_->GetWidgetAt(x, y, widget, x_in_selected_widget,
                                            y_in_selected_widget))
      return true;
  }
  if (vertical_scroll_bar_) {
    if (vertical_scroll_bar_->GetWidgetAt(x, y, widget, x_in_selected_widget,
                                          y_in_selected_widget))
      return true;
  }

  // Check our contents
  x += scroll_x_;
  y += scroll_y_;
  MaybeUpdateContentSize();
  return content_->GetWidgetAt(x, y, widget, x_in_selected_widget,
                               y_in_selected_widget);
}

ScrollContainer* ScrollContainer::ScrollTo(float x, float y) {
  if (x == scroll_x_ && y == scroll_y_) return this;
  ScrollToInternal(x, y);
  InvalidateRender();
  return this;
}

double ScrollContainer::GetScrollX() const { return scroll_x_; }
double ScrollContainer::GetScrollY() const { return scroll_y_; }

ScrollContainer* ScrollContainer::ShowVerticalScrollBar(bool show) {
  if (show_vertical_scroll_bar_ == show) return this;
  show_vertical_scroll_bar_ = show;
  RebuildLayout();
  return this;
}

ScrollContainer* ScrollContainer::ShowHorizontalScrollBar(bool show) {
  if (show_horizontal_scroll_bar_ == show) return this;
  show_horizontal_scroll_bar_ = show;
  RebuildLayout();
  return this;
}

bool ScrollContainer::IsShowingHorizontalScrollBar() const {
  return show_horizontal_scroll_bar_;
}

bool ScrollContainer::IsShowingVerticalScrollBar() const {
  return show_vertical_scroll_bar_;
}

ScrollContainer* ScrollContainer::SetOverlayImageEffect(
    std::shared_ptr<ImageEffect> overlay_image_effect) {
  if (overlay_image_effect_ == overlay_image_effect) return this;
  overlay_image_effect_ = overlay_image_effect;
  InvalidateRender();
  return this;
}

std::shared_ptr<ImageEffect> ScrollContainer::GetOverlayImageEffect() {
  return overlay_image_effect_;
}

std::shared_ptr<Widget> ScrollContainer::GetInnerContainer() {
  return inner_container_;
}

ScrollContainer::ScrollContainer(std::shared_ptr<ParentlessWidget> content,
                                 bool show_vertical_scroll_bar,
                                 bool show_horizontal_scroll_bar)
    : content_(content),
      show_vertical_scroll_bar_(show_vertical_scroll_bar),
      show_horizontal_scroll_bar_(show_horizontal_scroll_bar_),
      last_calculated_width_(0.0f),
      last_calculated_height_(0.0f),
      last_content_width_(0.0f),
      last_content_height_(0.0f) {
  SetPadding(YGEdgeAll, 0);
  SetBorderColor(kContainerBorderColor);
  SetBorderRadius(kContainerBorderRadius);
  SetBorderWidth(kContainerBorderWidth);
  SetBorderColor(kContainerBorderColor);
  SetClipContents(true);
  YGNodeSetDirtiedFunc(yoga_node_, &ScrollContainer::LayoutDirtied);
}

void ScrollContainer::Draw(DrawContext& draw_context) {
  // Draw the container.
  Container::Draw(draw_context);

  // Draw inner shadow over content but behind scroll bars.
  if (overlay_image_effect_) {
    SkPaint p;

    float x = GetLeft() + draw_context.offset_x;
    float y = GetTop() + draw_context.offset_y;
    float width = GetCalculatedWidth();
    float height = GetCalculatedHeight();

    p.setAntiAlias(true);
    p.setImageFilter(overlay_image_effect_->GetSkiaImageFilter());
    p.setColor(0xFFFFFFFF);
    p.setStyle(SkPaint::kFill_Style);
    draw_context.skia_canvas->drawRoundRect({x, y, x + width, y + height},
                                            border_radius_, border_radius_, p);
  }

  if (!show_horizontal_scroll_bar_ && !show_vertical_scroll_bar_) return;
  MaybeUpdateScrollBars();

  float old_offset_x = draw_context.offset_x;
  float old_offset_y = draw_context.offset_y;
  draw_context.offset_x += GetLeft();
  draw_context.offset_y += GetTop();

  // Draw the scroll bars.
  if (show_horizontal_scroll_bar_ && horizontal_scroll_bar_) {
    static_cast<std::shared_ptr<Widget>>(horizontal_scroll_bar_)
        ->Draw(draw_context);
  }

  if (show_vertical_scroll_bar_ && vertical_scroll_bar_) {
    static_cast<std::shared_ptr<Widget>>(vertical_scroll_bar_)
        ->Draw(draw_context);
  }
  draw_context.offset_x = old_offset_x;
  draw_context.offset_y = old_offset_y;
}

void ScrollContainer::DrawContents(DrawContext& draw_context) {
  float old_offset_x = draw_context.offset_x;
  float old_offset_y = draw_context.offset_y;
  draw_context.offset_x += GetLeft() - scroll_x_;
  draw_context.offset_y += GetTop() - scroll_y_;

  MaybeUpdateContentSize();

  content_->Draw(draw_context);

  draw_context.offset_x = old_offset_x;
  draw_context.offset_y = old_offset_y;
}

void ScrollContainer::RebuildLayout() {
  RemoveChildren();

  // Make sure the scroll bars exist or not.
  if (show_horizontal_scroll_bar_) {
    if (!horizontal_scroll_bar_) {
      horizontal_scroll_bar_ = ScrollBar::Create();
      horizontal_scroll_bar_->SetDirection(ScrollBar::Direction::HORIZONTAL)
          ->OnScroll(
              [this](float value) { ScrollToInternal(value, scroll_y_); })
          ->SetWidthPercent(100);
    }
  } else {
    horizontal_scroll_bar_.reset();
  }

  if (show_vertical_scroll_bar_) {
    if (!vertical_scroll_bar_) {
      vertical_scroll_bar_ = ScrollBar::Create();
      vertical_scroll_bar_->SetDirection(ScrollBar::Direction::VERTICAL)
          ->OnScroll(
              [this](float value) { ScrollToInternal(scroll_x_, value); })
          ->SetHeightPercent(100);
    }
  } else {
    vertical_scroll_bar_.reset();
  }

  if (show_vertical_scroll_bar_ || show_horizontal_scroll_bar_) {
    if (!inner_container_) {
      inner_container_ = std::make_shared<Widget>();
      inner_container_->SetFlexGrow(1.0f);
      inner_container_->SetFlexShrink(1.0f);
    }
  } else {
    inner_container_.reset();
  }

  if (show_vertical_scroll_bar_ && show_horizontal_scroll_bar_) {
    // TODO
  } else if (show_horizontal_scroll_bar_) {
    SetFlexDirection(YGFlexDirectionColumn);
    inner_container_->SetWidthPercent(100);
    inner_container_->SetMaxWidthPercent(100);
    AddChildren({inner_container_, horizontal_scroll_bar_});
  } else if (show_vertical_scroll_bar_) {
    SetFlexDirection(YGFlexDirectionRow);
    inner_container_->SetHeightPercent(100);
    inner_container_->SetMaxHeightPercent(100);
    AddChildren({inner_container_, vertical_scroll_bar_});
  }

  InvalidateScrollBars();
}

void ScrollContainer::LayoutDirtied(const YGNode* node) {
  ScrollContainer* scroll_container = (ScrollContainer*)YGNodeGetContext(node);
  scroll_container->MaybeUpdateContentSize();
  scroll_container->InvalidateScrollBars();
  scroll_container->InvalidateRender();
}

void ScrollContainer::InvalidateScrollBars() {
  scroll_bars_invalidated_ = true;
}

void ScrollContainer::MaybeUpdateScrollBars() {
  if (!scroll_bars_invalidated_) return;

  MaybeUpdateContentSize();
  content_->MaybeRecalculateLayout();
  auto contents = content_->GetContents();
  float content_width = contents->GetCalculatedWidthWithMargin();
  float content_height = contents->GetCalculatedHeightWithMargin();

  last_content_width_ = content_width;
  last_content_height_ = content_height;

  if (show_horizontal_scroll_bar_ && horizontal_scroll_bar_) {
    float view_width = inner_container_->GetCalculatedWidth();
    horizontal_scroll_bar_->SetPosition(0.0f, std::max(content_width, 0.0f),
                                        scroll_x_, view_width);
  }

  if (show_vertical_scroll_bar_ && vertical_scroll_bar_) {
    float view_height = inner_container_->GetCalculatedHeight();

    vertical_scroll_bar_->SetPosition(0.0f, std::max(content_height, 0.0f),
                                      scroll_y_, view_height);
  }

  scroll_bars_invalidated_ = false;
}

void ScrollContainer::ScrollToInternal(float x, float y) {
  if (scroll_x_ == x && scroll_y_ == y) return;

  scroll_x_ = x;
  scroll_y_ = y;
  InvalidateRender();
}

void ScrollContainer::MaybeUpdateContentSize() {
  if (content_) {
    // Work out how much space there is for the contents.
    double calculated_width = GetCalculatedWidth();
    double calculated_height = GetCalculatedHeight();
    if (show_horizontal_scroll_bar_) calculated_height -= kScrollBarThickness;

    if (show_vertical_scroll_bar_) calculated_width -= kScrollBarThickness;

    // Has the inner size changed?
    if (calculated_width != last_calculated_width_ ||
        calculated_height != last_calculated_height_) {
      // Let the inner contents know that we've changed size.

      content_->SetParentSize(calculated_width, calculated_height);

      last_calculated_width_ = calculated_width;
      last_calculated_height_ = calculated_height;

      InvalidateScrollBars();
    }
  }
}

void ScrollContainer::CheckIfContentsChangedSize() {
  content_->MaybeRecalculateLayout();
  auto contents = content_->GetContents();
  float content_width = contents->GetCalculatedWidthWithMargin();
  float content_height = contents->GetCalculatedHeightWithMargin();

  if (content_width != last_content_width_ ||
      content_height != last_content_height_)
    InvalidateScrollBars();
  last_content_width_ = content_width;
  last_content_height_ = content_height;
}

}  // namespace ui
}  // namespace perception