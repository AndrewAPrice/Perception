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

#include "perception/ui/components/resizable_container.h"

#include <algorithm>

#include "perception/ui/components/block.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"

namespace perception {

template class UniqueIdentifiableType<ui::components::ResizableContainerItem>;
template class UniqueIdentifiableType<ui::components::ResizableContainer>;

namespace ui {
namespace components {

namespace {

bool IsItemFixed(std::shared_ptr<Node> item) {
  if (!item) return true;
  if (auto r = item->Get<ResizableContainerItem>()) {
    return r->GetBehavior() == ResizableContainerItem::Behavior::Fixed;
  }
  return item->GetLayout().GetFlexGrow() == 0.0f;
}

}  // namespace

ResizableContainerItem::ResizableContainerItem() : behavior_(Behavior::Fixed) {}

void ResizableContainerItem::SetNode(std::weak_ptr<Node> node) { node_ = node; }

void ResizableContainerItem::SetBehavior(Behavior behavior) {
  behavior_ = behavior;
}

ResizableContainerItem::Behavior ResizableContainerItem::GetBehavior() const {
  return behavior_;
}

ResizableContainer::ResizableContainer()
    : direction_(YGFlexDirectionRow),
      is_dragging_(false),
      dragging_index_(-1),
      drag_start_(0.0f),
      start_size_a_(0.0f),
      start_size_b_(0.0f) {}

void ResizableContainer::SetNode(std::weak_ptr<Node> node) { node_ = node; }

void ResizableContainer::Initialize(YGFlexDirection direction) {
  direction_ = direction;
  if (node_.expired()) return;
  auto strong_node = node_.lock();

  std::vector<std::shared_ptr<Node>> original_children(
      strong_node->GetChildren().begin(), strong_node->GetChildren().end());
  if (original_children.size() < 2) return;

  strong_node->RemoveChildren();
  items_ = original_children;

  strong_node->GetLayout().SetGap(0.0f);

  for (size_t i = 0; i < items_.size(); ++i) {
    if (IsItemFixed(items_[i])) {
      items_[i]->GetLayout().SetFlexGrow(0.0f);
      items_[i]->GetLayout().SetFlexShrink(0.0f);
    }
    if (i > 0) {
      auto splitter = BuildSplitter(i - 1);
      strong_node->AddChild(splitter);
    }
    strong_node->AddChild(items_[i]);
  }
}

std::shared_ptr<Node> ResizableContainer::BuildSplitter(size_t index) {
  auto self = shared_from_this();
  auto splitter = Node::Empty(
      [this](Layout& layout) {
        if (direction_ == YGFlexDirectionRow) {
          layout.SetWidth(7.0f);
          layout.SetHeightPercent(100.0f);
        } else {
          layout.SetHeight(7.0f);
          layout.SetWidthPercent(100.0f);
        }
        layout.SetFlexShrink(0.0f);
        layout.SetJustifyContent(YGJustifyCenter);
        layout.SetAlignItems(YGAlignCenter);
      },
      [](Block& block) { block.SetFillColor(0x00000000); },
      Block::SolidColor(0xFFD1D5DB, [this](Layout& layout) {
        if (direction_ == YGFlexDirectionRow) {
          layout.SetWidth(1.0f);
          layout.SetHeightPercent(100.0f);
        } else {
          layout.SetHeight(1.0f);
          layout.SetWidthPercent(100.0f);
        }
      }));

  splitter->SetCursor((direction_ == YGFlexDirectionRow)
                          ? window::Cursor::ResizeHorizontal
                          : window::Cursor::ResizeVertical);

  splitter->OnMouseHover([self, splitter, index](const Point& point) {
    self->OnSplitterHover(index, splitter, point);
  });
  splitter->OnMouseLeave(
      [self, splitter]() { self->OnSplitterLeave(splitter); });
  splitter->OnMouseButtonDown(
      [self, splitter, index](const Point& point, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          self->OnSplitterMouseDown(index, splitter, point);
        }
      });
  splitter->OnMouseButtonUp(
      [self, splitter](const Point& point, window::MouseButton button) {
        if (button == window::MouseButton::Left) {
          self->OnSplitterMouseUp(splitter);
        }
      });

  return splitter;
}

void ResizableContainer::OnSplitterMouseDown(size_t index,
                                             std::shared_ptr<Node> splitter,
                                             const Point& point) {
  if (index + 1 >= items_.size()) return;
  is_dragging_ = true;
  dragging_index_ = index;

  Point parent_point = point + splitter->GetPositionRelativeToParent();
  drag_start_ =
      (direction_ == YGFlexDirectionRow) ? parent_point.x : parent_point.y;

  auto left_item = items_[index];
  auto right_item = items_[index + 1];

  if (direction_ == YGFlexDirectionRow) {
    start_size_a_ = left_item->GetLayout().GetCalculatedWidth();
    start_size_b_ = right_item->GetLayout().GetCalculatedWidth();
    if (IsItemFixed(left_item)) {
      left_item->GetLayout().SetFlexGrow(0.0f);
      left_item->GetLayout().SetWidth(start_size_a_);
    }
    if (IsItemFixed(right_item)) {
      right_item->GetLayout().SetFlexGrow(0.0f);
      right_item->GetLayout().SetWidth(start_size_b_);
    }
  } else {
    start_size_a_ = left_item->GetLayout().GetCalculatedHeight();
    start_size_b_ = right_item->GetLayout().GetCalculatedHeight();
    if (IsItemFixed(left_item)) {
      left_item->GetLayout().SetFlexGrow(0.0f);
      left_item->GetLayout().SetHeight(start_size_a_);
    }
    if (IsItemFixed(right_item)) {
      right_item->GetLayout().SetFlexGrow(0.0f);
      right_item->GetLayout().SetHeight(start_size_b_);
    }
  }

  if (auto block = splitter->Get<Block>()) {
    block->SetFillColor(0xFF9CA3AF);
    splitter->Invalidate();
  }
}

void ResizableContainer::OnSplitterMouseUp(std::shared_ptr<Node> splitter) {
  if (!is_dragging_) return;
  is_dragging_ = false;
  dragging_index_ = -1;

  if (auto block = splitter->Get<Block>()) {
    block->SetFillColor(0x00000000);
    splitter->Invalidate();
  }
}

void ResizableContainer::OnSplitterLeave(std::shared_ptr<Node> splitter) {
  if (!is_dragging_) {
    if (auto block = splitter->Get<Block>()) {
      block->SetFillColor(0x00000000);
      splitter->Invalidate();
    }
  }
}

void ResizableContainer::OnSplitterHover(size_t index,
                                         std::shared_ptr<Node> splitter,
                                         const Point& point) {
  if (!is_dragging_) {
    if (auto block = splitter->Get<Block>()) {
      block->SetFillColor(0xFFD1D5DB);
      splitter->Invalidate();
    }
    return;
  }

  if (index != dragging_index_ || index + 1 >= items_.size()) return;

  Point parent_point = point + splitter->GetPositionRelativeToParent();
  float current_pos =
      (direction_ == YGFlexDirectionRow) ? parent_point.x : parent_point.y;
  float delta = current_pos - drag_start_;

  if (delta == 0.0f) return;

  auto left_item = items_[index];
  auto right_item = items_[index + 1];

  float new_size_a = std::max(20.0f, start_size_a_ + delta);
  float actual_delta = new_size_a - start_size_a_;

  float new_size_b = std::max(20.0f, start_size_b_ - actual_delta);
  actual_delta = start_size_b_ - new_size_b;

  if (actual_delta == 0.0f) return;

  if (direction_ == YGFlexDirectionRow) {
    bool fixed_a = IsItemFixed(left_item);
    bool fixed_b = IsItemFixed(right_item);
    if (!fixed_a && !fixed_b) {
      left_item->GetLayout().SetFlexGrow(start_size_a_ + actual_delta);
      left_item->GetLayout().SetWidthAuto();
      right_item->GetLayout().SetFlexGrow(start_size_b_ - actual_delta);
      right_item->GetLayout().SetWidthAuto();
    } else {
      if (fixed_a) {
        left_item->GetLayout().SetFlexGrow(0.0f);
        left_item->GetLayout().SetWidth(start_size_a_ + actual_delta);
      } else {
        left_item->GetLayout().SetWidthAuto();
      }
      if (fixed_b) {
        right_item->GetLayout().SetFlexGrow(0.0f);
        right_item->GetLayout().SetWidth(start_size_b_ - actual_delta);
      } else {
        right_item->GetLayout().SetWidthAuto();
      }
    }
  } else {
    bool fixed_a = IsItemFixed(left_item);
    bool fixed_b = IsItemFixed(right_item);
    if (!fixed_a && !fixed_b) {
      left_item->GetLayout().SetFlexGrow(start_size_a_ + actual_delta);
      left_item->GetLayout().SetHeightAuto();
      right_item->GetLayout().SetFlexGrow(start_size_b_ - actual_delta);
      right_item->GetLayout().SetHeightAuto();
    } else {
      if (fixed_a) {
        left_item->GetLayout().SetFlexGrow(0.0f);
        left_item->GetLayout().SetHeight(start_size_a_ + actual_delta);
      } else {
        left_item->GetLayout().SetHeightAuto();
      }
      if (fixed_b) {
        right_item->GetLayout().SetFlexGrow(0.0f);
        right_item->GetLayout().SetHeight(start_size_b_ - actual_delta);
      } else {
        right_item->GetLayout().SetHeightAuto();
      }
    }
  }

  if (!node_.expired()) {
    node_.lock()->Invalidate();
  }
}

}  // namespace components
}  // namespace ui
}  // namespace perception
