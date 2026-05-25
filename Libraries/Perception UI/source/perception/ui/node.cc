// Copyright 2021 Google LLC
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

#include "perception/ui/node.h"

#include <cmath>
#include <iostream>
#include <map>

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/layout.h"
#include "perception/ui/measurements.h"
#include "perception/window/mouse_button.h"

using ::perception::window::MouseButton;

namespace perception {
namespace ui {

Node::Node()
    : yoga_node_(YGNodeNew()),
      invalidate_when_dirtied_(false),
      invalidated_(false),
      handles_mouse_events_(false),
      scroll_offset_({.x = 0.0f, .y = 0.0f}) {
  YGNodeSetContext(yoga_node_, this);
}

Node::~Node() {
  for (auto& child : children_) child->SetParent({});

  // This does some extra work than merely deleting the node.
  YGNodeFree(yoga_node_);
}

std::weak_ptr<Node> Node::GetParent() { return parent_; }

Layout Node::GetLayout() { return Layout(yoga_node_); }

void Node::AddChildren(const std::vector<std::shared_ptr<Node>>& children) {
  for (auto child : children) AddChild(child);
}

void Node::AddChild(std::shared_ptr<Node> child) {
  child->SetParent(shared_from_this());
  YGNodeInsertChild(yoga_node_, child->yoga_node_, children_.size());
  children_.push_back(child);
}

void Node::RemoveChild(std::shared_ptr<Node> child) {
  child->SetParent({});
  children_.remove(child);
  YGNodeRemoveChild(yoga_node_, child->yoga_node_);
}

void Node::RemoveChildren() {
  for (auto child : children_) child->SetParent({});
  children_.clear();
  YGNodeRemoveAllChildren(yoga_node_);
}

const std::list<std::shared_ptr<Node>>& Node::GetChildren() {
  return children_;
}

void Node::Draw(DrawContext& draw_context) {
  if (IsHidden()) return;
  auto position = GetAreaRelativeToParent();

  Rectangle old_area = draw_context.area;
  draw_context.area.origin += position.origin;
  draw_context.area.size = position.size;

  if (!draw_context.area.Intersects(draw_context.clipping_bounds)) {
    draw_context.area = old_area;
    return;
  }

  invalidated_ = false;
  if (!on_draw_functions_.empty()) {
    for (const auto& draw_function : on_draw_functions_)
      draw_function(draw_context);
  }

  bool clip_children = GetLayout().GetOverflow() != YGOverflowVisible;
  if (clip_children) {
    auto intersecting_clipping_bounds =
        draw_context.clipping_bounds.Intersection(draw_context.area);
    if (intersecting_clipping_bounds &&
        intersecting_clipping_bounds->size.width > 0.0f &&
        intersecting_clipping_bounds->size.height > 0.0f) {
      Rectangle old_clipping_bounds = draw_context.clipping_bounds;
      draw_context.clipping_bounds = *intersecting_clipping_bounds;

      draw_context.skia_canvas->save();
      draw_context.skia_canvas->clipRect(
          SkRect::MakeXYWH(intersecting_clipping_bounds->origin.x,
                           intersecting_clipping_bounds->origin.y,
                           intersecting_clipping_bounds->size.width,
                           intersecting_clipping_bounds->size.height));

      DrawChildren(draw_context);

      draw_context.skia_canvas->restore();

      draw_context.clipping_bounds = old_clipping_bounds;
    }
  } else {
    DrawChildren(draw_context);
  }

  if (!on_draw_post_children_functions_.empty()) {
    for (const auto& draw_function : on_draw_post_children_functions_)
      draw_function(draw_context);
  }

  draw_context.area = old_area;
}

void Node::SetMeasureFunction(
    std::function<Size(float width, YGMeasureMode width_mode, float height,
                       YGMeasureMode height_mode)>
        measure_function) {
  if (measure_function_) {
    std::cerr << "There are multiple measure functions set on the same node."
              << std::endl;
  }

  if (!measure_function_ && measure_function) {
    YGNodeSetMeasureFunc(yoga_node_, &Node::Measure);
  } else if (measure_function_ && !measure_function) {
    YGNodeSetMeasureFunc(yoga_node_, nullptr);
  }
  measure_function_ = measure_function;
  YGNodeMarkDirty(yoga_node_);
}

void Node::Remeasure() { GetLayout().MarkDirty(); }

void Node::SetHitTestFunction(
    std::function<bool(const Point& point, const Size& size)>
        hit_test_function) {
  if (hit_test_function_) {
    std::cerr << "There are multiple hit test functions set on the same node."
              << std::endl;
  }
  hit_test_function_ = hit_test_function;
}

void Node::OnDraw(
    std::function<void(const DrawContext& context)> draw_function) {
  on_draw_functions_.push_back(draw_function);
  InvalidateWhenDirtied();
}

void Node::OnDrawPostChildren(
    std::function<void(const DrawContext& context)> draw_function) {
  on_draw_post_children_functions_.push_back(draw_function);
  InvalidateWhenDirtied();
}

void Node::OnMouseHover(
    std::function<void(const Point& point)> mouse_hover_function) {
  on_mouse_hover_functions_.push_back(mouse_hover_function);
  handles_mouse_events_ = true;
}

void Node::MouseHover(const Point& point) {
  for (const auto& handler : on_mouse_hover_functions_) handler(point);
}

void Node::OnMouseLeave(std::function<void()> mouse_leave_function) {
  on_mouse_leave_functions_.push_back(mouse_leave_function);
  handles_mouse_events_ = true;
}

void Node::MouseLeave() {
  for (const auto& handler : on_mouse_leave_functions_) handler();
}

void Node::OnMouseButtonDown(
    std::function<void(const Point& point, window::MouseButton button)>
        mouse_button_down_function) {
  on_mouse_button_down_functions_.push_back(mouse_button_down_function);
}

void Node::MouseButtonDown(const Point& point, window::MouseButton button) {
  for (const auto& handler : on_mouse_button_down_functions_)
    handler(point, button);
}

void Node::OnMouseButtonUp(
    std::function<void(const Point& point, window::MouseButton button)>
        mouse_button_up_function) {
  on_mouse_button_up_functions_.push_back(mouse_button_up_function);
}

void Node::MouseButtonUp(const Point& point, window::MouseButton button) {
  for (const auto& handler : on_mouse_button_up_functions_)
    handler(point, button);
}


void Node::OnInvalidate(std::function<void()> invalidate_function) {
  on_invalidate_functions_.push_back(invalidate_function);
  InvalidateWhenDirtied();
}

bool Node::GetNodesAt(
    const Point& point,
    const std::function<void(Node& node, const Point& point_in_node)>&
        on_hit_node) {
  if (IsHidden()) return false;
  auto position = GetAreaRelativeToParent();
  if (!position.Contains(point)) return false;
  Point point_in_here = point - position.origin;

  if (hit_test_function_ && !hit_test_function_(point_in_here, position.size))
    return false;

  Layout layout = GetLayout();
  Point point_without_margin{.x = point.x - layout.GetLeft() + scroll_offset_.x,
                             .y = point.y - layout.GetTop() + scroll_offset_.y};

  bool child_blocks_hit_test = false;

  // Walk backwards (from top to bottom).
  bool hit_child = true;
  for (auto itr = children_.rbegin(); itr != children_.rend(); itr++) {
    if ((*itr)->GetNodesAt(point_without_margin, on_hit_node)) {
      // This node blocks the nodes behind it from being hit tested.
      child_blocks_hit_test = true;
      break;
    }
  }
  // This node.
  on_hit_node(*this, point_in_here);

  // Returns whether to block hit testing the node behind this node.
  return child_blocks_hit_test || BlocksHitTest();
}

void Node::SetBlocksHitTest(bool blocks_hit_test) {
  blocks_hit_test_ = blocks_hit_test;
}

bool Node::BlocksHitTest() { return blocks_hit_test_; }

void Node::Invalidate() {
  if (invalidated_) return;
  invalidated_ = true;
  if (!parent_.expired()) parent_.lock()->Invalidate();

  for (const auto& handler : on_invalidate_functions_) handler();
}

void Node::SetCursor(window::Cursor cursor) { cursor_ = cursor; }

std::optional<window::Cursor> Node::GetCursor() const { return cursor_; }

bool Node::DoesHandleMouseLeaveEvents() {
  return !on_mouse_leave_functions_.empty();
}

bool Node::IsHidden() {
  return YGNodeStyleGetDisplay(yoga_node_) == YGDisplayNone;
}

const Point& Node::GetOffset() const { return scroll_offset_; }

void Node::SetOffset(const Point& offset) {
  if (offset == scroll_offset_) return;
  scroll_offset_ = offset;
  Invalidate();
}

std::shared_ptr<Node> Node::ToSharedPtr() { return shared_from_this(); }

Rectangle Node::GetAreaRelativeToParent() {
  return {.origin = GetPositionRelativeToParent(), .size = GetSize()};
}

Point Node::GetPositionRelativeToParent() {
  Layout layout = GetLayout();
  return {.x = layout.GetLeft(), .y = layout.GetTop()};
}

Size Node::GetSize() {
  Layout layout = GetLayout();
  return {.width = layout.GetCalculatedWidth(),
          .height = layout.GetCalculatedHeight()};
}

void Node::SetParent(std::weak_ptr<Node> parent) { parent_ = parent; }

void Node::DrawChildren(DrawContext& draw_context) {
  if (children_.empty()) return;
  Layout layout = GetLayout();
  Point old_origin = draw_context.area.origin;
  draw_context.area.origin -= scroll_offset_;
  for (auto& child : children_) child->Draw(draw_context);
  draw_context.area.origin = old_origin;
}

void Node::InvalidateWhenDirtied() {
  if (invalidate_when_dirtied_) return;
  YGNodeSetDirtiedFunc(yoga_node_, &Node::LayoutDirtied);
  invalidate_when_dirtied_ = true;
}

void Node::LayoutDirtied(const YGNode* node) {
  Node* ui_node = (Node*)YGNodeGetContext(node);
  ui_node->Invalidate();
}

YGSize Node::Measure(const YGNode* node, float width, YGMeasureMode width_mode,
                     float height, YGMeasureMode height_mode) {
  if (width_mode == YGMeasureModeExactly &&
      height_mode == YGMeasureModeExactly) {
    return {.width = width, .height = height};
  }

  Node* ui_node = (Node*)YGNodeGetContext(node);
  if (ui_node->measure_function_) {
    Size size =
        ui_node->measure_function_(width, width_mode, height, height_mode);
    return {.width = size.width, .height = size.height};
  } else {
    return {.width = CalculateMeasuredLength(width_mode, width, 0.0f),
            .height = CalculateMeasuredLength(height_mode, height, 0.0f)};
  }
}

void Node::PrintHierarchy(int indent) {
  std::string indent_str(indent * 2, ' ');

  std::cout << indent_str << "{\n";
  std::cout << indent_str << "  \"node\": \"" << this << "\",\n";
  std::cout << indent_str << "  \"hidden\": " << (IsHidden() ? "true" : "false")
            << ",\n";

  // Print calculated layout values
  auto layout = GetLayout();
  std::cout << indent_str << "  \"calculated_layout\": {\n";
  std::cout << indent_str << "    \"left\": " << layout.GetLeft() << ",\n";
  std::cout << indent_str << "    \"top\": " << layout.GetTop() << ",\n";
  std::cout << indent_str << "    \"width\": " << layout.GetCalculatedWidth()
            << ",\n";
  std::cout << indent_str << "    \"height\": " << layout.GetCalculatedHeight()
            << "\n";
  std::cout << indent_str << "  }";

  // Check for components
  if (auto label = Get<components::Label>()) {
    std::cout << ",\n" << indent_str << "  \"label\": {\n";
    std::cout << indent_str << "    \"text\": \"" << label->GetText()
              << "\",\n";
    std::cout << indent_str << "    \"color\": \"0x" << std::hex
              << label->GetColor() << std::dec << "\"\n";
    std::cout << indent_str << "  }";
  }

  if (auto block = Get<components::Block>()) {
    std::cout << ",\n" << indent_str << "  \"block\": {\n";
    std::cout << indent_str << "    \"fill_color\": \"0x" << std::hex
              << block->GetFillColor() << std::dec << "\",\n";
    std::cout << indent_str << "    \"border_color\": \"0x" << std::hex
              << block->GetBorderColor() << std::dec << "\",\n";
    std::cout << indent_str
              << "    \"border_width\": " << block->GetBorderWidth() << "\n";
    std::cout << indent_str << "  }";
  }

  if (scroll_offset_.x != 0.0f || scroll_offset_.y != 0.0f) {
    std::cout << ",\n" << indent_str << "  \"scroll_offset\": {\n";
    std::cout << indent_str << "    \"x\": " << scroll_offset_.x << ",\n";
    std::cout << indent_str << "    \"y\": " << scroll_offset_.y << "\n";
    std::cout << indent_str << "  }";
  }

  if (auto scroll_bar = Get<components::ScrollBar>()) {
    std::cout << ",\n" << indent_str << "  \"scroll_bar\": {\n";
    std::cout << indent_str << "    \"direction\": "
              << (scroll_bar->GetDirection() ==
                          components::ScrollBar::Direction::VERTICAL
                      ? "\"vertical\""
                      : "\"horizontal\"")
              << ",\n";
    std::cout << indent_str << "    \"value\": " << scroll_bar->GetValue()
              << "\n";
    std::cout << indent_str << "  }";
  }

  if (auto scroll_container = Get<components::ScrollContainer>()) {
    std::cout << ",\n" << indent_str << "  \"scroll_container\": {\n";
    std::cout << indent_str << "    \"content_position\": {"
              << "\"x\": " << scroll_container->ContentPosition().x
              << ", \"y\": " << scroll_container->ContentPosition().y << "},\n";
    std::cout << indent_str << "    \"content_size\": {"
              << "\"width\": " << scroll_container->ContentSize().width
              << ", \"height\": " << scroll_container->ContentSize().height
              << "},\n";
    std::cout << indent_str << "    \"container_size\": {"
              << "\"width\": " << scroll_container->ContainerSize().width
              << ", \"height\": " << scroll_container->ContainerSize().height
              << "}\n";
    std::cout << indent_str << "  }";
  }

  // Children
  if (!children_.empty()) {
    std::cout << ",\n" << indent_str << "  \"children\": [\n";
    bool first = true;
    for (auto& child : children_) {
      if (!first) {
        std::cout << ",\n";
      }
      first = false;
      child->PrintHierarchy(indent + 2);
    }
    std::cout << "\n" << indent_str << "  ]\n";
  } else {
    std::cout << "\n";
  }

  std::cout << indent_str << "}";
  if (indent == 0) {
    std::cout << std::endl;
  }
}

}  // namespace ui
}  // namespace perception
