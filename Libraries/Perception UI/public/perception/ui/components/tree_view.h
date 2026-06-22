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

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/theme.h"
#include "perception/window/keyboard_key_event.h"

namespace perception {
namespace ui {
namespace components {

class TreeViewItem;

class TreeView : public UniqueIdentifiableType<TreeView>,
                 public std::enable_shared_from_this<TreeView> {
 public:
  TreeView();

  void SetNode(std::weak_ptr<Node> node);
  std::weak_ptr<Node> GetNode() const;

  template <typename... Modifiers>
  static std::shared_ptr<Node> Create(Modifiers... modifiers) {
    auto content = Container::VerticalContainer([](Layout& layout) {
      layout.SetWidthPercent(100.0f);
      layout.SetGap(kTreeViewVerticalGap);
      layout.SetPadding(YGEdgeAll, kTreeViewPadding);
    });

    auto scroll = ScrollContainer::BidirectionalScrollContainer(
        content,
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetHeightPercent(100.0f);
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetMinHeight(0.0f);
        },
        [](Block& block) { block.SetFillColor(kTreeViewBackgroundColor); },
        [content](TreeView& tv) { tv.content_container_ = content; },
        modifiers...);

    return scroll;
  }

  std::shared_ptr<Node> GetContentContainer() const {
    return content_container_.lock();
  }

  void SetSelectedItem(std::shared_ptr<TreeViewItem> item,
                       bool notify_listener = true);

  std::shared_ptr<TreeViewItem> GetSelectedItem() const;

  void OnSelect(std::function<void()> on_select);

  void Focus();
  void Unfocus();
  bool HasFocus() const;

  void OnCanDrop(
      std::function<bool(std::shared_ptr<TreeViewItem>,
                         std::shared_ptr<TreeViewItem>)> can_drop);
  void OnDrop(
      std::function<void(std::shared_ptr<TreeViewItem>,
                         std::shared_ptr<TreeViewItem>)> on_drop);

  std::vector<std::shared_ptr<TreeViewItem>> GetVisibleItems() const;

  void SetPotentialDragItem(std::shared_ptr<TreeViewItem> item);
  std::shared_ptr<TreeViewItem> GetPotentialDragItem() const;
  void ClearPotentialDragItem();
  bool IsDragging() const;
  void StartDrag(std::shared_ptr<TreeViewItem> item, Point window_pt);
  void UpdateDrag(Point window_pt);
  void EndDrag();

 private:
  std::weak_ptr<Node> node_;
  std::weak_ptr<Node> content_container_;
  std::weak_ptr<TreeViewItem> selected_item_;
  std::vector<std::function<void()>> on_select_;
  std::shared_ptr<Focusable> focusable_;

  std::vector<std::function<bool(std::shared_ptr<TreeViewItem>,
                                 std::shared_ptr<TreeViewItem>)>> can_drop_;
  std::vector<std::function<void(std::shared_ptr<TreeViewItem>,
                                 std::shared_ptr<TreeViewItem>)>> on_drop_;

  bool is_dragging_;
  Point drag_start_mouse_;
  std::weak_ptr<TreeViewItem> potential_drag_item_;
  std::weak_ptr<TreeViewItem> dragged_item_;
  std::weak_ptr<TreeViewItem> current_drop_target_;
  std::shared_ptr<Node> drag_overlay_;
  std::shared_ptr<Node> drag_ghost_;

  void HandleKeyDown(const window::KeyboardKeyEvent& event);
  bool CanDrop(std::shared_ptr<TreeViewItem> source,
               std::shared_ptr<TreeViewItem> target);
  std::shared_ptr<TreeViewItem> HitTest(Point window_pt) const;
};

class TreeViewItem : public UniqueIdentifiableType<TreeViewItem>,
                     public std::enable_shared_from_this<TreeViewItem> {
 public:
  TreeViewItem();

  void SetNode(std::weak_ptr<Node> node);
  std::weak_ptr<Node> GetNode() const;

  template <typename... Modifiers>
  static std::shared_ptr<Node> Item(
      std::string_view text,
      const std::vector<std::shared_ptr<Node>>& children = {},
      Modifiers... modifiers) {
    auto label = Label::BasicLabel(
        text, [](Label& label) { label.SetColor(kTreeViewItemTextColor); });
    return Item(label, children, modifiers...);
  }

  template <typename... Modifiers>
  static std::shared_ptr<Node> Item(
      std::shared_ptr<Node> content,
      const std::vector<std::shared_ptr<Node>>& children = {},
      Modifiers... modifiers) {
    auto children_container = Container::VerticalContainer([](Layout& layout) {
      layout.SetWidthPercent(100.0f);
      layout.SetPadding(YGEdgeLeft, kTreeViewIndent);
      layout.SetGap(kTreeViewVerticalGap);
      layout.SetDisplay(YGDisplayNone);
    });

    for (const auto& child : children) {
      if (child) children_container->AddChild(child);
    }

    auto toggle_button = Node::Empty([](Layout& layout) {
      layout.SetWidth(kTreeViewToggleWidth);
      layout.SetHeight(kTreeViewToggleHeight);
      layout.SetFlexShrink(0.0f);
    });

    auto content_container = Container::HorizontalContainer(
        [](Layout& layout) {
          layout.SetFlexGrow(1.0f);
          layout.SetFlexShrink(1.0f);
          layout.SetMinHeight(0.0f);
          layout.SetMinWidth(0.0f);
          layout.SetAlignItems(YGAlignCenter);
        },
        content);

    auto row_container = Container::HorizontalContainer(
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetPadding(YGEdgeHorizontal, kTreeViewItemHorizontalPadding);
          layout.SetGap(kTreeViewItemGap);
        },
        toggle_button, content_container);

    auto node = Container::VerticalContainer(
        [](Layout& layout) {
          layout.SetWidthPercent(100.0f);
          layout.SetFlexShrink(0.0f);
          layout.SetGap(kTreeViewVerticalGap);
        },
        [&](TreeViewItem& item) {
          item.SetContainers(row_container, toggle_button, content_container,
                             children_container);
        },
        row_container, children_container, modifiers...);

    return node;
  }

  void SetContainers(std::weak_ptr<Node> row_container,
                     std::weak_ptr<Node> toggle_button,
                     std::weak_ptr<Node> content_container,
                     std::weak_ptr<Node> children_container);

  void SetExpanded(bool expanded);
  bool IsExpanded() const;

  void Select(bool scroll_into_view = true, bool notify_listener = true);

  void SetSelected(bool selected);
  bool IsSelected() const;

  void ExpandParents();

  void ScrollIntoView();

  void OnSelect(std::function<void()> on_select);

  void NotifySelect();

  enum class DragOverState { NONE, VALID, INVALID };
  void SetDragOverState(DragOverState state);

  bool IsAncestorOf(std::shared_ptr<TreeViewItem> other) const;
  std::shared_ptr<Node> GetRowContainer() const;
  std::shared_ptr<Node> GetChildrenContainer() const;
  std::shared_ptr<Node> GetContentContainer() const;

  void OnContextMenu(std::function<void(Point screen_pt)> handler);

 private:
  std::weak_ptr<Node> node_;
  std::weak_ptr<Node> row_container_;
  std::weak_ptr<Node> toggle_button_;
  std::weak_ptr<Node> content_container_;
  std::weak_ptr<Node> children_container_;

  bool is_expanded_;
  bool is_selected_;

  std::vector<std::function<void()>> on_select_;
  std::vector<std::function<void(Point)>> on_context_menu_;

  void DrawToggle(const DrawContext& draw_context);
  void ToggleExpanded();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::TreeView>;
extern template class UniqueIdentifiableType<ui::components::TreeViewItem>;

}  // namespace perception
