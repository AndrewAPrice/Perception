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
#include "perception/ui/theme.h"

using ::permebuf::perception::devices::MouseButton;

namespace perception {
namespace ui {

std::shared_ptr<ScrollContainer> ScrollContainer::Create(
    std::shared_ptr<Widget> content, bool show_vertical_scroll_bar,
    bool show_horizontal_scroll_bar) {
  std::shared_ptr<ScrollContainer> scroll_container(new ScrollContainer(
      show_vertical_scroll_bar, show_horizontal_scroll_bar));
  scroll_container->content_ = content;
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

std::shared_ptr<Widget> ScrollContainer::GetInnerContainer() {
  return inner_container_;
}

ScrollContainer::ScrollContainer(bool show_vertical_scroll_bar,
                                 bool show_horizontal_scroll_bar)
    : show_vertical_scroll_bar_(show_vertical_scroll_bar),
      show_horizontal_scroll_bar_(show_horizontal_scroll_bar_) {
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
  } else {
    AddChild(content_);
  }

  InvalidateScrollBars();
}

void ScrollContainer::LayoutDirtied(YGNode* node) {
  ScrollContainer* scroll_container = (ScrollContainer*)YGNodeGetContext(node);
  scroll_container->InvalidateScrollBars();
  scroll_container->InvalidateRender();
}

void ScrollContainer::InvalidateScrollBars() {
  scroll_bars_invalidated_ = true;
}

void ScrollContainer::MaybeUpdateScrollBars() {
  if (!scroll_bars_invalidated_) return;

  if (show_horizontal_scroll_bar_ && horizontal_scroll_bar_) {
    float content_width = content_->GetCalculatedWidthWithMargin();
    float view_width = inner_container_->GetCalculatedWidth();
    horizontal_scroll_bar_->SetPosition(0.0f, std::max(content_width - view_width, 0.0f),
                                        scroll_x_, view_width);
  }

  if (show_vertical_scroll_bar_ && vertical_scroll_bar_) {
    float content_height = content_->GetCalculatedHeightWithMargin();
    float view_height = inner_container_->GetCalculatedHeight();
    vertical_scroll_bar_->SetPosition(0.0f, std::max(content_height - view_height, 0.0f),
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

}  // namespace ui
}  // namespace perception