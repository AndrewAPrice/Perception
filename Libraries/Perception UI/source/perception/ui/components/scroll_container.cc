// Copyright 2025 Google LLC
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
#include "perception/ui/components/scroll_container.h"

#include "perception/ui/components/scroll_bar.h"
/*
namespace perception {
namespace ui {
namespace components {

void ScrollContainer::SetNode(std::weak_ptr<Node> node) {}

void ScrollContainer::SetContentPosition(Point& position) {
  auto scroll_container = scroll_container_.lock();
  if (!scroll_container) return;

  scroll_container->SetOffset(position);
}

Point ScrollContainer::ContentPosition() {
  auto scroll_container = scroll_container_.lock();
  if (!scroll_container) return Point{0.0f, 0.0f};

  return scroll_container->GetOffset();
}

void ScrollContainer::SetContentAndContainerNodes(
    std::weak_ptr<Node> scroll_content, std::weak_ptr<Node> scroll_container) {
  scroll_content_ = scroll_content;
  scroll_container_ = scroll_container;

  auto strong_scroll_container = scroll_container_.lock();
  auto strong_scroll_content = scroll_content_.lock();

  if (!strong_scroll_container || !strong_scroll_content) return;

  std::weak_ptr<ScrollContainer> weak_this = shared_from_this();
  auto invalidation_handler = [weak_this] {
    auto strong_this = weak_this.lock();
    if (strong_this) strong_this->UpdateScrollBars();
  };

  strong_scroll_container->OnInvalidate(invalidation_handler);
  strong_scroll_content->OnInvalidate(invalidation_handler);
}

void ScrollContainer::SetHorizontalScrollBar(
    std::weak_ptr<ScrollBar> horiziontal_scroll_bar) {
  scroll_bars_[0] = horiziontal_scroll_bar;
  RegisterScrollBarListener(scroll_bars_[0]);
}

void ScrollContainer::SetVerticalScrollBar(
    std::weak_ptr<ScrollBar> vertical_scroll_bar) {
  scroll_bars_[1] = vertical_scroll_bar;
  RegisterScrollBarListener(scroll_bars_[1]);
}

Size ScrollContainer::ContentSize() {
  auto scroll_content = scroll_content_.lock();
  if (!scroll_content) return Size{0.0f, 0.0f};

  auto scroll_content_layout = scroll_content.Layout();
  return {.width = scroll_content_layout.GetRight(),
          .height = scroll_content_layout.GetBottom()};
}

Size ScrollContainer::ContainerSize() {
  auto scroll_container = scroll_container_.lock();
  if (!scroll_container) return Size{0.0f, 0.0f};

  return scroll_content.Layout().GetCalculatedSize();
}

void ScrollContainer::RegisterScrollBarListener(
    std::weak_ptr<ScrollBar> scroll_bar) {
  auto strong_scroll_bar = scroll_bar.lock();
  if (!strong_scroll_bar) return;

  std::weak_ptr<ScrollContainer> weak_this = shared_from_this();
  strong_scroll_bar->OnScroll([weak_this] {
    auto strong_this = weak_this.lock();
    if (strong_this) strong_this->MoveContentToScrollBarPosition();
  })
}

void ScrollContainer::UpdateScrollBars() {
  std::shared_ptr<ScrollBar> scroll_bars[2];
  for (int i = 0; i < 2; i++) scroll_bars[i] = scroll_bars_[i].lock();

  // Return if there are no scroll bars.
  if (!scroll_bars[0] && !scroll_bars[1]) return;

  Point content_position = GetContentPosition();
  Size content_size = GetContentSize();
  Size container_size = GetContainerSize();

  for (int i = 0; i < 2; i++) {
    if (!scroll_bars[i]) continue;
    scroll_bars[i]->SetValue(0.0f, content_size[i], content_position[i],
                             container_size[i]);
  }
}

void ScrollContainer::MoveContentToScrollBarPosition() {
  SetContentPosition({.x = GetScrollBarValue(0), .y = GetScrollBarValue(1)});
}

float ScrollContainer::GetScrollBarValue(int dimension) {
  auto scroll_bar = scroll_bars_[dimension].lock();
  return scroll_bar ? scroll_bar->GetValue() : 0.0f;
}

}  // namespace components
}  // namespace ui
}  // namespace perception
  */