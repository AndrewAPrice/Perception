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

#pragma once

#include <memory>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/container.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"

namespace perception {
namespace ui {
namespace components {

// Below is an example of how to create a resizable container with 3 columns,
// where all the columns are draggable, however if the container itself changes
// size (such as the window resizing) the left and right sizes are "anchored"
// and the middle dynamically changes size.
//
//   auto left_sidebar = Container::VerticalContainer(
//       [](ResizableContainerItem& item) {
//         item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
//       },
//       [](Layout& layout) { layout.SetWidth(200.0f); });
//
//   auto main_content = Container::VerticalContainer(
//       [](ResizableContainerItem& item) {
//         item.SetBehavior(ResizableContainerItem::Behavior::Flex);
//       },
//       [](Layout& layout) { layout.SetFlexGrow(1.0f); });
//
//   auto right_sidebar = Container::VerticalContainer(
//       [](ResizableContainerItem& item) {
//         item.SetBehavior(ResizableContainerItem::Behavior::Fixed);
//       },
//       [](Layout& layout) { layout.SetWidth(250.0f); });
//
//   auto split_view = ResizableContainer::HorizontalContainer(
//       [](Layout& layout) { layout.SetWidthPercent(100.0f); },
//       left_sidebar, main_content, right_sidebar);

// Configuration component attached to child items of a ResizableContainer
// to dictate how they behave when dragged or when the outer container resizes.
class ResizableContainerItem
    : public UniqueIdentifiableType<ResizableContainerItem> {
 public:
  enum class Behavior {
    // Stays the same size it was dragged/set to regardless of outer container
    // resize.
    Fixed,
    // Grows/shrinks proportionally with the outer container.
    Flex
  };

  ResizableContainerItem();

  void SetNode(std::weak_ptr<Node> node);

  void SetBehavior(Behavior behavior);
  Behavior GetBehavior() const;

 private:
  std::weak_ptr<Node> node_;
  Behavior behavior_;
};

// A container that lays out its children with draggable splitters between them
// to allow resizing columns or rows.
class ResizableContainer
    : public UniqueIdentifiableType<ResizableContainer>,
      public std::enable_shared_from_this<ResizableContainer> {
 public:
  ResizableContainer();

  // A container that lays out its contents horizontally with resizable columns.
  template <typename... Modifiers>
  static std::shared_ptr<Node> HorizontalContainer(Modifiers... modifiers) {
    return Container::HorizontalContainer(
        modifiers..., [](ResizableContainer& container) {
          container.Initialize(YGFlexDirectionRow);
        });
  }

  // A container that lays out its contents vertically with resizable rows.
  template <typename... Modifiers>
  static std::shared_ptr<Node> VerticalContainer(Modifiers... modifiers) {
    return Container::VerticalContainer(
        modifiers..., [](ResizableContainer& container) {
          container.Initialize(YGFlexDirectionColumn);
        });
  }

  void SetNode(std::weak_ptr<Node> node);
  void Initialize(YGFlexDirection direction);

 private:
  std::weak_ptr<Node> node_;
  YGFlexDirection direction_;
  std::vector<std::shared_ptr<Node>> items_;

  bool is_dragging_;
  size_t dragging_index_;
  float drag_start_;
  float start_size_a_;
  float start_size_b_;

  std::shared_ptr<Node> BuildSplitter(size_t index);

  void OnSplitterMouseDown(size_t index, std::shared_ptr<Node> splitter,
                           const Point& point);
  void OnSplitterMouseUp(std::shared_ptr<Node> splitter);
  void OnSplitterHover(size_t index, std::shared_ptr<Node> splitter,
                       const Point& point);
  void OnSplitterLeave(std::shared_ptr<Node> splitter);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<
    ui::components::ResizableContainerItem>;
extern template class UniqueIdentifiableType<
    ui::components::ResizableContainer>;

}  // namespace perception
