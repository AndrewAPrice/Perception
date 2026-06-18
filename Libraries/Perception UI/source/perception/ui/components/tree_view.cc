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

#include "perception/ui/components/tree_view.h"

#include <iostream>

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/keyboard.h"
#include "perception/ui/theme.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::TreeView>;
template class UniqueIdentifiableType<ui::components::TreeViewItem>;

namespace ui {
namespace components {

TreeView::TreeView()
    : is_dragging_(false),
      drag_start_mouse_({0, 0}) {}

void TreeView::SetNode(std::weak_ptr<Node> node) {
  node_ = node;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  focusable_ = strong_node->GetOrAdd<Focusable>();

  strong_node->OnMouseButtonDown(
      [this](const Point&, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          if (focusable_) focusable_->Focus();
        }
      });

  focusable_->OnKeyDown([this](const window::KeyboardKeyEvent& event) {
    HandleKeyDown(event);
  });
}
std::weak_ptr<Node> TreeView::GetNode() const { return node_; }

void TreeView::SetSelectedItem(std::shared_ptr<TreeViewItem> item,
                               bool notify_listener) {
  auto old = selected_item_.lock();
  if (old == item) return;

  if (old) {
    old->SetSelected(false);
  }

  selected_item_ = item;

  if (item) {
    item->SetSelected(true);
  }

  if (notify_listener) {
    for (auto& cb : on_select_) cb();
    if (item) {
      item->NotifySelect();
    }
  }
}

std::shared_ptr<TreeViewItem> TreeView::GetSelectedItem() const {
  return selected_item_.lock();
}

void TreeView::OnSelect(std::function<void()> on_select) {
  on_select_.push_back(on_select);
}

void TreeView::Focus() {
  if (focusable_) focusable_->Focus();
}

void TreeView::Unfocus() {
  if (focusable_) focusable_->Unfocus();
}

bool TreeView::HasFocus() const {
  return focusable_ && focusable_->HasFocus();
}

void TreeView::OnCanDrop(
    std::function<bool(std::shared_ptr<TreeViewItem>,
                       std::shared_ptr<TreeViewItem>)> can_drop) {
  can_drop_.push_back(can_drop);
}

void TreeView::OnDrop(
    std::function<void(std::shared_ptr<TreeViewItem>,
                       std::shared_ptr<TreeViewItem>)> on_drop) {
  on_drop_.push_back(on_drop);
}

namespace {
void CollectVisibleItems(std::shared_ptr<Node> parent_container,
                         std::vector<std::shared_ptr<TreeViewItem>>& items) {
  if (!parent_container) return;
  for (const auto& child : parent_container->GetChildren()) {
    if (auto item = child->Get<TreeViewItem>()) {
      items.push_back(item);
      if (item->IsExpanded()) {
        CollectVisibleItems(item->GetChildrenContainer(), items);
      }
    }
  }
}
}  // namespace

std::vector<std::shared_ptr<TreeViewItem>> TreeView::GetVisibleItems() const {
  std::vector<std::shared_ptr<TreeViewItem>> items;
  CollectVisibleItems(node_.lock(), items);
  return items;
}

void TreeView::SetPotentialDragItem(std::shared_ptr<TreeViewItem> item) {
  potential_drag_item_ = item;
}

std::shared_ptr<TreeViewItem> TreeView::GetPotentialDragItem() const {
  return potential_drag_item_.lock();
}

void TreeView::ClearPotentialDragItem() {
  potential_drag_item_.reset();
}

bool TreeView::IsDragging() const {
  return is_dragging_;
}

void TreeView::StartDrag(std::shared_ptr<TreeViewItem> item, Point window_pt) {
  if (!item || node_.expired() || is_dragging_) return;
  auto strong_node = node_.lock();

  std::shared_ptr<Node> root = strong_node;
  while (true) {
    auto parent = root->GetParent().lock();
    if (!parent) break;
    root = parent;
  }

  drag_start_mouse_ = window_pt;
  dragged_item_ = item;
  is_dragging_ = true;

  auto overlay = std::make_shared<Node>();
  overlay->GetLayout().SetPositionType(YGPositionTypeAbsolute);
  overlay->GetLayout().SetWidthPercent(100.f);
  overlay->GetLayout().SetHeightPercent(100.f);
  overlay->GetLayout().SetPosition(YGEdgeLeft, 0.f);
  overlay->GetLayout().SetPosition(YGEdgeTop, 0.f);
  overlay->SetBlocksHitTest(false);

  auto ghost = std::make_shared<Node>();
  ghost->GetLayout().SetPositionType(YGPositionTypeAbsolute);
  ghost->GetLayout().SetPosition(YGEdgeLeft, window_pt.x + 12.0f);
  ghost->GetLayout().SetPosition(YGEdgeTop, window_pt.y + 12.0f);
  ghost->SetBlocksHitTest(false);

  auto block = ghost->GetOrAdd<Block>();
  block->SetFillColor(0xEE1F2937);
  block->SetBorderRadius(6.0f);
  ghost->GetLayout().SetPadding(YGEdgeHorizontal, 8.0f);
  ghost->GetLayout().SetPadding(YGEdgeVertical, 4.0f);

  auto lbl = Label::BasicLabel("Dragging...", [](Label& l) {
    l.SetColor(0xFFFFFFFF);
  });
  ghost->AddChild(lbl);
  drag_ghost_ = ghost;
  overlay->AddChild(ghost);

  drag_overlay_ = overlay;
  root->AddChild(overlay);
  root->Invalidate();

  UpdateDrag(window_pt);
}

void TreeView::UpdateDrag(Point window_pt) {
  if (!is_dragging_) return;
  if (drag_ghost_) {
    drag_ghost_->GetLayout().SetPosition(YGEdgeLeft, window_pt.x + 12.0f);
    drag_ghost_->GetLayout().SetPosition(YGEdgeTop, window_pt.y + 12.0f);
  }

  auto hit = HitTest(window_pt);
  auto old_target = current_drop_target_.lock();
  if (hit != old_target) {
    if (old_target) old_target->SetDragOverState(TreeViewItem::DragOverState::NONE);
    current_drop_target_ = hit;
    if (hit) {
      bool can = CanDrop(dragged_item_.lock(), hit);
      hit->SetDragOverState(can ? TreeViewItem::DragOverState::VALID
                                : TreeViewItem::DragOverState::INVALID);
    }
  }
  if (auto r = node_.lock()) {
    auto root = r;
    while (root->GetParent().lock()) root = root->GetParent().lock();
    root->Invalidate();
  }
}

void TreeView::EndDrag() {
  if (auto target = current_drop_target_.lock()) {
    target->SetDragOverState(TreeViewItem::DragOverState::NONE);
  }
  auto source = dragged_item_.lock();
  auto target = current_drop_target_.lock();

  if (is_dragging_ && source && target && CanDrop(source, target)) {
    for (auto& cb : on_drop_) {
      cb(source, target);
    }
  }

  if (drag_overlay_) {
    if (auto parent = drag_overlay_->GetParent().lock()) {
      parent->RemoveChild(drag_overlay_);
    }
    drag_overlay_.reset();
  }
  drag_ghost_.reset();
  dragged_item_.reset();
  current_drop_target_.reset();
  potential_drag_item_.reset();
  is_dragging_ = false;

  if (!node_.expired()) node_.lock()->Invalidate();
}

void TreeView::HandleKeyDown(const window::KeyboardKeyEvent& event) {
  KeyCode key = static_cast<KeyCode>(event.key);
  auto item = selected_item_.lock();
  auto visible_items = GetVisibleItems();
  if (visible_items.empty()) return;

  if (!item) {
    if (key == KeyCode::DownArrow || key == KeyCode::UpArrow) {
      visible_items.front()->Select(true, true);
    }
    return;
  }

  if (key == KeyCode::UpArrow) {
    for (size_t i = 0; i < visible_items.size(); ++i) {
      if (visible_items[i] == item) {
        if (i > 0) {
          visible_items[i - 1]->Select(true, true);
        }
        break;
      }
    }
    return;
  }

  if (key == KeyCode::DownArrow) {
    for (size_t i = 0; i < visible_items.size(); ++i) {
      if (visible_items[i] == item) {
        if (i + 1 < visible_items.size()) {
          visible_items[i + 1]->Select(true, true);
        }
        break;
      }
    }
    return;
  }

  if (key == KeyCode::RightArrow) {
    if (!item->IsExpanded()) {
      auto children = item->GetChildrenContainer();
      if (children && !children->GetChildren().empty()) {
        item->SetExpanded(true);
      }
    } else {
      for (size_t i = 0; i < visible_items.size(); ++i) {
        if (visible_items[i] == item) {
          if (i + 1 < visible_items.size() &&
              item->IsAncestorOf(visible_items[i + 1])) {
            visible_items[i + 1]->Select(true, true);
          }
          break;
        }
      }
    }
    return;
  }

  if (key == KeyCode::LeftArrow) {
    if (item->IsExpanded()) {
      item->SetExpanded(false);
    } else {
      if (auto node = item->GetNode().lock()) {
        auto parent = node->GetParent().lock();
        while (parent) {
          if (auto parent_item = parent->Get<TreeViewItem>()) {
            parent_item->Select(true, true);
            break;
          }
          parent = parent->GetParent().lock();
        }
      }
    }
    return;
  }
}

bool TreeView::CanDrop(std::shared_ptr<TreeViewItem> source,
                       std::shared_ptr<TreeViewItem> target) {
  if (!source || !target) return false;
  if (source == target) return false;
  if (source->IsAncestorOf(target)) return false;
  for (const auto& cb : can_drop_) {
    if (!cb(source, target)) return false;
  }
  return true;
}

std::shared_ptr<TreeViewItem> TreeView::HitTest(Point window_pt) const {
  auto items = GetVisibleItems();
  for (const auto& item : items) {
    if (auto row = item->GetRowContainer()) {
      Point pos = row->GetAbsolutePosition();
      Size sz = row->GetSize();
      if (pos.x <= window_pt.x && window_pt.x <= pos.x + sz.width &&
          pos.y <= window_pt.y && window_pt.y <= pos.y + sz.height) {
        return item;
      }
    }
  }
  return nullptr;
}

TreeViewItem::TreeViewItem() : is_expanded_(false), is_selected_(false) {}

void TreeViewItem::SetNode(std::weak_ptr<Node> node) { node_ = node; }
std::weak_ptr<Node> TreeViewItem::GetNode() const { return node_; }

void TreeViewItem::SetContainers(std::weak_ptr<Node> row_container,
                                 std::weak_ptr<Node> toggle_button,
                                 std::weak_ptr<Node> content_container,
                                 std::weak_ptr<Node> children_container) {
  row_container_ = row_container;
  toggle_button_ = toggle_button;
  content_container_ = content_container;
  children_container_ = children_container;

  if (auto row = row_container_.lock()) {
    auto block = row->GetOrAdd<components::Block>();
    block->SetBorderRadius(kTreeViewItemBorderRadius);
  }

  if (auto toggle = toggle_button_.lock()) {
    toggle->OnDraw(std::bind_front(&TreeViewItem::DrawToggle, this));
    toggle->SetBlocksHitTest(true);
    toggle->OnMouseButtonDown(
        [this](const Point&, window::MouseButton button) {
          if (button == window::MouseButton::Left) {
            ToggleExpanded();
          }
        });
  }

  if (auto content = content_container_.lock()) {
    content->SetBlocksHitTest(true);
    content->OnMouseButtonDown(
        [this](const Point& pt, window::MouseButton button) {
          if (button == window::MouseButton::Right) {
            if (!on_context_menu_.empty() && content_container_.lock()) {
              Point screen_pt = content_container_.lock()->GetAbsolutePosition() + pt;
              for (auto& cb : on_context_menu_) cb(screen_pt);
            }
          } else if (button == window::MouseButton::Left) {
            Select(false, true);
            if (auto strong_node = node_.lock()) {
              auto curr = strong_node->GetParent().lock();
              std::shared_ptr<TreeView> tv;
              while (curr) {
                if (auto t = curr->Get<TreeView>()) {
                  tv = t;
                  break;
                }
                curr = curr->GetParent().lock();
              }
              if (tv) {
                tv->SetPotentialDragItem(shared_from_this());
              }
            }
          }
        });

    content->OnMouseHover([this](const Point& pt) {
      if (auto strong_node = node_.lock()) {
        auto curr = strong_node->GetParent().lock();
        std::shared_ptr<TreeView> tv;
        while (curr) {
          if (auto t = curr->Get<TreeView>()) {
            tv = t;
            break;
          }
          curr = curr->GetParent().lock();
        }
        if (tv && content_container_.lock()) {
          Point window_pt = content_container_.lock()->GetAbsolutePosition() + pt;
          if (tv->IsDragging()) {
            tv->UpdateDrag(window_pt);
          } else if (tv->GetPotentialDragItem() == shared_from_this()) {
            if (auto row = row_container_.lock()) {
              Point row_pos = row->GetAbsolutePosition();
              Size row_sz = row->GetSize();
              if (window_pt.x < row_pos.x || window_pt.x > row_pos.x + row_sz.width ||
                  window_pt.y < row_pos.y || window_pt.y > row_pos.y + row_sz.height) {
                tv->StartDrag(shared_from_this(), window_pt);
              }
            }
          }
        }
      }
    });

    content->OnMouseButtonUp(
        [this](const Point&, window::MouseButton button) {
          if (button == window::MouseButton::Left) {
            if (auto strong_node = node_.lock()) {
              auto curr = strong_node->GetParent().lock();
              std::shared_ptr<TreeView> tv;
              while (curr) {
                if (auto t = curr->Get<TreeView>()) {
                  tv = t;
                  break;
                }
                curr = curr->GetParent().lock();
              }
              if (tv) {
                if (tv->IsDragging()) {
                  tv->EndDrag();
                }
                tv->ClearPotentialDragItem();
              }
            }
          }
        });
  }
}

void TreeViewItem::SetExpanded(bool expanded) {
  if (is_expanded_ == expanded) return;
  is_expanded_ = expanded;

  if (auto children = children_container_.lock()) {
    children->GetLayout().SetDisplay(is_expanded_ ? YGDisplayFlex
                                                  : YGDisplayNone);
  }

  if (!node_.expired()) {
    node_.lock()->Invalidate();
  }
}

bool TreeViewItem::IsExpanded() const { return is_expanded_; }

void TreeViewItem::Select(bool scroll_into_view, bool notify_listener) {
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  std::shared_ptr<TreeView> tree_view;
  auto curr = strong_node->GetParent().lock();
  while (curr) {
    if (auto tv = curr->Get<TreeView>()) {
      tree_view = tv;
      break;
    }
    curr = curr->GetParent().lock();
  }

  if (tree_view) {
    tree_view->SetSelectedItem(shared_from_this(), notify_listener);
    tree_view->Focus();
  } else {
    SetSelected(true);
    if (notify_listener) {
      NotifySelect();
    }
  }

  if (scroll_into_view) {
    ExpandParents();
    ScrollIntoView();
  }
}

void TreeViewItem::SetSelected(bool selected) {
  if (is_selected_ == selected) return;
  is_selected_ = selected;

  if (auto row = row_container_.lock()) {
    auto block = row->GetOrAdd<components::Block>();
    block->SetFillColor(is_selected_ ? kTextBoxSelectionColor : 0);
    block->SetBorderWidth(0.0f);
  }
}

bool TreeViewItem::IsSelected() const { return is_selected_; }

void TreeViewItem::ExpandParents() {
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  auto curr = strong_node->GetParent().lock();
  while (curr) {
    if (auto item = curr->Get<TreeViewItem>()) {
      item->SetExpanded(true);
    }
    curr = curr->GetParent().lock();
  }
}

void TreeViewItem::ScrollIntoView() {
  if (node_.expired()) return;
  auto strong_node = node_.lock();
  auto row = row_container_.lock();
  if (!row) return;

  auto curr = strong_node->GetParent().lock();
  while (curr) {
    if (auto scroll_container = curr->Get<ScrollContainer>()) {
      scroll_container->ScrollIntoView(row);
      break;
    }
    curr = curr->GetParent().lock();
  }
}

void TreeViewItem::OnSelect(std::function<void()> on_select) {
  on_select_.push_back(on_select);
}

void TreeViewItem::NotifySelect() {
  for (const auto& cb : on_select_) cb();
}

void TreeViewItem::OnContextMenu(std::function<void(Point)> handler) {
  on_context_menu_.push_back(handler);
}

void TreeViewItem::SetDragOverState(DragOverState state) {
  if (auto row = row_container_.lock()) {
    auto block = row->GetOrAdd<components::Block>();
    if (state == DragOverState::VALID) {
      block->SetFillColor(0xFFD1FAE5);
      block->SetBorderColor(0xFF10B981);
      block->SetBorderWidth(1.0f);
    } else if (state == DragOverState::INVALID) {
      block->SetFillColor(0xFFFEE2E2);
      block->SetBorderColor(0xFFEF4444);
      block->SetBorderWidth(1.0f);
    } else {
      block->SetFillColor(is_selected_ ? kTextBoxSelectionColor : 0);
      block->SetBorderWidth(0.0f);
    }
  }
}

bool TreeViewItem::IsAncestorOf(std::shared_ptr<TreeViewItem> other) const {
  if (!other || node_.expired()) return false;
  auto curr = other->GetNode().lock();
  while (curr) {
    if (auto item = curr->Get<TreeViewItem>()) {
      if (item.get() == this) return true;
    }
    curr = curr->GetParent().lock();
  }
  return false;
}

std::shared_ptr<Node> TreeViewItem::GetRowContainer() const {
  return row_container_.lock();
}

std::shared_ptr<Node> TreeViewItem::GetChildrenContainer() const {
  return children_container_.lock();
}

std::shared_ptr<Node> TreeViewItem::GetContentContainer() const {
  return content_container_.lock();
}

void TreeViewItem::DrawToggle(const DrawContext& draw_context) {
  auto children = children_container_.lock();
  if (!children || children->GetChildren().empty()) return;

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kLabelTextColor);
  paint.setStyle(SkPaint::kFill_Style);

  float x = draw_context.area.origin.x + 4.0f;
  float y = draw_context.area.origin.y + 6.0f;

  if (is_expanded_) {
    SkPoint pts[3] = {
        {x, y + 2.0f}, {x + 8.0f, y + 2.0f}, {x + 4.0f, y + 6.0f}};
    draw_context.skia_canvas->drawPath(SkPath::Polygon({pts, 3}, true), paint);
  } else {
    SkPoint pts[3] = {
        {x + 2.0f, y}, {x + 6.0f, y + 4.0f}, {x + 2.0f, y + 8.0f}};
    draw_context.skia_canvas->drawPath(SkPath::Polygon({pts, 3}, true), paint);
  }
}

void TreeViewItem::ToggleExpanded() { SetExpanded(!is_expanded_); }

}  // namespace components
}  // namespace ui
}  // namespace perception
