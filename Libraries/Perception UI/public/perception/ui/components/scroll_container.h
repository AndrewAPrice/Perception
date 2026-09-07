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

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

// A container with optional scroll bars.
class ScrollContainer : public UniqueIdentifiableType<ScrollContainer>,
                        public std::enable_shared_from_this<ScrollContainer> {
 public:
  ScrollContainer();

  // Modifiers to override interior content padding.
  static auto ContentPadding(float padding) {
    return [padding](ScrollContainer& container) {
      container.SetContentPadding(padding);
    };
  }

  static auto ContentPadding(YGEdge edge, float padding) {
    return [edge, padding](ScrollContainer& container) {
      container.SetContentPadding(edge, padding);
    };
  }

  static auto NoContentPadding() {
    return
        [](ScrollContainer& container) { container.SetContentPadding(0.0f); };
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> BidirectionalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<Node> scroll_container_node;
    std::shared_ptr<ScrollBar> horizontal_scroll_bar, vertical_scroll_bar;

    ApplyDefaultPadding(scroll_content);

    auto node = Container::HorizontalContainer(
        [](Block& block) {
          block.SetFillColor(kScrollContainerBackgroundColor);
          block.SetBorderColor(kScrollContainerBorderColor);
          block.SetBorderWidth(kScrollContainerBorderWidth);
          block.SetBorderRadius(kScrollContainerBorderRadius);
          block.SetClipContents(true);
        },
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container_node);
          container.SetHorizontalScrollBar(horizontal_scroll_bar);
          container.SetVerticalScrollBar(vertical_scroll_bar);
        },
        [](Layout& layout) { layout.SetGap(0.0f); },
        Container::VerticalContainer(
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
              layout.SetMinHeight(0.0f);
              layout.SetMinWidth(0.0f);
              layout.SetGap(0.0f);
            },
            Node::Empty(
                [](Layout& layout) {
                  layout.SetOverflow(YGOverflowScroll);
                  layout.SetFlexGrow(1.0f);
                  layout.SetFlexShrink(1.0f);
                  layout.SetMinHeight(0.0f);
                  layout.SetMinWidth(0.0f);
                },
                &scroll_container_node, scroll_content),
            ScrollBar::HorizontalScrollBar(&horizontal_scroll_bar)),
        ScrollBar::VerticalScrollBar(&vertical_scroll_bar), modifiers...);

    scroll_content->GetLayout().SetFlexShrink(0.0f);
    node->GetLayout().SetMinHeight(0.0f);
    node->GetLayout().SetMinWidth(0.0f);
    node->GetLayout().SetFlexShrink(1.0f);

    return node;
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> VerticalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<Node> scroll_container_node;
    std::shared_ptr<ScrollBar> vertical_scroll_bar;

    ApplyDefaultPadding(scroll_content);

    auto node = Container::HorizontalContainer(
        [](Block& block) {
          block.SetFillColor(kScrollContainerBackgroundColor);
          block.SetBorderColor(kScrollContainerBorderColor);
          block.SetBorderWidth(kScrollContainerBorderWidth);
          block.SetBorderRadius(kScrollContainerBorderRadius);
          block.SetClipContents(true);
        },
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container_node);
          container.SetVerticalScrollBar(vertical_scroll_bar);
        },
        [](Layout& layout) { layout.SetGap(0.0f); },
        Node::Empty(
            [](Layout& layout) {
              layout.SetOverflow(YGOverflowScroll);
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
              layout.SetMinHeight(0.0f);
            },
            &scroll_container_node, scroll_content),
        ScrollBar::VerticalScrollBar(&vertical_scroll_bar), modifiers...);

    scroll_content->GetLayout().SetFlexShrink(0.0f);
    node->GetLayout().SetMinHeight(0.0f);
    node->GetLayout().SetFlexShrink(1.0f);

    return node;
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> HorizontalScrollContainer(
      std::shared_ptr<Node> scroll_content, Modifiers... modifiers) {
    std::shared_ptr<Node> scroll_container_node;
    std::shared_ptr<ScrollBar> horizontal_scroll_bar;

    ApplyDefaultPadding(scroll_content);

    auto node = Container::VerticalContainer(
        [](Block& block) {
          block.SetFillColor(kScrollContainerBackgroundColor);
          block.SetBorderColor(kScrollContainerBorderColor);
          block.SetBorderWidth(kScrollContainerBorderWidth);
          block.SetBorderRadius(kScrollContainerBorderRadius);
          block.SetClipContents(true);
        },
        [&](ScrollContainer& container) {
          container.SetContentAndContainerNodes(scroll_content,
                                                scroll_container_node);
          container.SetHorizontalScrollBar(horizontal_scroll_bar);
        },
        [](Layout& layout) { layout.SetGap(0.0f); },
        Node::Empty(
            [](Layout& layout) {
              layout.SetOverflow(YGOverflowScroll);
              layout.SetFlexGrow(1.0f);
              layout.SetFlexShrink(1.0f);
              layout.SetMinWidth(0.0f);
            },
            &scroll_container_node, scroll_content),
        ScrollBar::HorizontalScrollBar(&horizontal_scroll_bar), modifiers...);

    scroll_content->GetLayout().SetFlexShrink(0.0f);
    node->GetLayout().SetMinWidth(0.0f);
    node->GetLayout().SetFlexShrink(1.0f);

    return node;
  }

  void SetNode(std::weak_ptr<Node> node);

  // Sets the padding on the interior content.
  void SetContentPadding(float padding);
  void SetContentPadding(YGEdge edge, float padding);

  void SetContentPosition(const Point& position);
  Point ContentPosition();
  void SetContentAndContainerNodes(std::weak_ptr<Node> scroll_content,
                                   std::weak_ptr<Node> scroll_container);

  void SetHorizontalScrollBar(std::weak_ptr<ScrollBar> horizontal_scroll_bar);
  void SetVerticalScrollBar(std::weak_ptr<ScrollBar> vertical_scroll_bar);

  Size ContentSize();
  Size ContainerSize();

  void ScrollIntoView(std::shared_ptr<Node> node);

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
  static void ApplyDefaultPadding(std::shared_ptr<Node> scroll_content);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::ScrollContainer>;

}  // namespace perception