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
#pragma once
#include <memory>

#include "perception/ui/components/container.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
/*
namespace perception {
namespace ui {
namespace components {

// A container with optional scroll bars.
class ScrollContainer : std::enable_shared_from_this<ScrollContainer> {
 public:
  template <typename... Modifiers>
  static std::shared_ptr<Node> BidirectionalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<ScrollContainer> scroll_container;
    std::shared_ptr<ScrollBar> horizontal_scroll_bar, vertical_scroll_bar;

    auto node = Container::HorizontalContainer(
        Container::VerticalContainer(
            Node::Empty(
                [](Layout& layout) { layout.SetOverflow(YGOverflowScroll); },
                &scroll_container, scroll_content),
            ScrollBar::HoriziontalScrollBar(&horizontal_scroll_bar)),
        ScrollBar::VerticalScrollBar(&vertical_scroll_bar),
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container);
          container.SetHorizontalScrollBar(horizontal_scroll_bar);
          container.SeVerticalScrollBar(vertical_scroll_bar);
        });

    return node;
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> VerticalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<ScrollContainer> scroll_container;
    std::shared_ptr<ScrollBar> vertical_scroll_bar;

    auto node = Container::HorizontalContainer(
        Node::Empty(
            [](Layout& layout) { layout.SetOverflow(YGOverflowScroll); },
            &scroll_container, scroll_content),
        ScrollBar::VerticalScrollBar(&vertical_scroll_bar),
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container);
          container.SeVerticalScrollBar(vertical_scroll_bar);
        });

    return node;
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> HorizontalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<ScrollContainer> scroll_container;
    std::shared_ptr<ScrollBar> horizontal_scroll_bar;

    auto node = Container::VerticalContainer(
        Node::Empty(
            [](Layout& layout) { layout.SetOverflow(YGOverflowScroll); },
            &scroll_container, scroll_content),
        ScrollBar::HoriziontalScrollBar(&horizontal_scroll_bar),
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container);
          container.SetHorizontalScrollBar(horizontal_scroll_bar);
        });

    return node;
  }

  void SetNode(std::weak_ptr<Node> node);

  void SetContentPosition(Point& position);
  Point ContentPosition();
  void SetContentAndContainerNodes(std::weak_ptr<Node> scroll_content,
                                   std::weak_ptr<Node> scroll_container);

  void SetHorizontalScrollBar(std::weak_ptr<ScrollBar> horizontal_scroll_bar);
  void SeVerticalScrollBar(std::weak_ptr<ScrollBar> vertical_scroll_bar);

  Size ContentSize();
  Size ContainerSize();

 private:
  std::weak_ptr<Node> scroll_content_;
  std::weak_ptr<Node> scroll_container_;
  bool has_calculated_content_size_;
  Size content_size_;

  std::weak_ptr<ScrollBar> scroll_bars_[2];

  void RegisterScrollBarListener(std::weak_ptr<ScrollBar> scroll_bar);
  void UpdateScrollBars();
  void MoveContentToScrollBarPosition();
  float GetScrollBarValue(int dimension);
};

}  // namespace components
}  // namespace ui
}  // namespace perception
*/