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

#pragma once

#include <functional>
#include <iostream>  // DO NOT SUBMIT
#include <list>
#include <map>
#include <memory>
#include <vector>

#include "perception/devices/mouse_listener.h"
#include "perception/type_id.h"
#include "perception/ui/image_effect.h"
#include "perception/ui/layout.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/size.h"
#include "perception/window/mouse_button.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {

class Point;
class Size;

constexpr int kFillParent = -1;
constexpr int kFitContent = -2;

class DrawContext;

class Node : public std::enable_shared_from_this<Node> {
 public:
  Node();
  ~Node();

  // Creates an empty node.
  template <typename... Modifiers>
  static std::shared_ptr<Node> Empty(Modifiers... modifiers) {
    auto node = std::make_shared<Node>();
    node->Apply(modifiers...);
    return std::move(node);
  }

  std::shared_ptr<Node> ToSharedPtr();
  // Returns the parent of this node.
  std::weak_ptr<Node> GetParent();
  // Returns the controller for manipulating the layout of this node.
  Layout GetLayout();

  template <typename T>
  void Add(std::shared_ptr<T> component) {
    auto itr = components_.find(T::TypeId());
    if (itr != components_.end()) return;

    component->SetNode(shared_from_this());
    components_.insert({T::TypeId(), component});
  }

  template <typename T>
  void Add() {
    Add(std::make_shared<T>());
  }

  template <typename T>
  std::shared_ptr<T> Get() {
    auto itr = components_.find(T::TypeId());
    if (itr == components_.end()) return {};

    return std::static_pointer_cast<T>(itr->second);
  }

  template <typename T>
  std::shared_ptr<T> GetOrAdd() {
    auto itr = components_.find(T::TypeId());
    if (itr == components_.end()) {
      auto component = std::make_shared<T>();
      Add(component);
      return component;
    }

    return std::static_pointer_cast<T>(itr->second);
  }

  void AddChildren(const std::vector<std::shared_ptr<Node>>& children);
  void AddChild(std::shared_ptr<Node> child);
  void RemoveChild(std::shared_ptr<Node> child);
  void RemoveChildren();
  const std::list<std::shared_ptr<Node>>& GetChildren();

  void Draw(DrawContext& draw_context);

  // Sets the function to call to measure the node. The function returns the
  // size of the node. Each dimension has passed in size and mode. If the
  // dimension's mode is:
  //  - YGMeasureModeUndefined - Input dimension is irrelevant and can be any
  //  size it wants.
  //  - YGMeasureModeExactly - Dimension must be exactly the input dimension.
  //  - YGMeasureModeAtMost - Dimension can be any any size it wants up to the
  //  input dimension.
  void SetMeasureFunction(
      std::function<Size(float width, YGMeasureMode width_mode, float height,
                         YGMeasureMode height_mode)>
          measure_function);

  // Notifies the node that it needs remeasuring. This only applies if a
  // measure function has been set.
  void Remeasure();

  // Sets a custom hit test function. The function returns whether a point falls
  // in the bounds of this primitive. Basic bounds checking that `point` lies
  // between 0 and `size` should have been done by the caller.
  void SetHitTestFunction(
      std::function<bool(const Point& point, const Size& size)>
          hit_test_function);

  // Adds a function to use to draw this node. The `draw_context` contains
  // the information as to where to draw this primitive, such as `offset` and
  // `size`.
  void OnDraw(std::function<void(const DrawContext& context)> draw_function);

  // Adds a function to use to draw this node, after any children have been
  // drawn. The `draw_context` contains the information as to where to draw this
  // primitive, such as `offset` and `size`.
  void OnDrawPostChildren(
      std::function<void(const DrawContext& context)> draw_function);

  // Adds a function to call when the mouse hovers over this element.
  void OnMouseHover(
      std::function<void(const Point& point)> mouse_hover_function);

  // Tells the node the mouse has hovered the node.
  void MouseHover(const Point& point);

  // Adds a function to call when the mouse was hovering over this element but
  // has then left.
  void OnMouseLeave(std::function<void()> mouse_leave_function);

  // Tells the node the mouse has left the node.
  void MouseLeave();

  // Adds a function to call when the mouse button was pressed down while over
  // this element.
  void OnMouseButtonDown(
      std::function<void(const Point& point, window::MouseButton button)>
          mouse_button_down_function);

  // Tells the node the mouse has pressed down a button while over the node.
  void MouseButtonDown(const Point& point, window::MouseButton button);

  // Adds a function to call when the mouse button was released while over
  // this element.
  void OnMouseButtonUp(
      std::function<void(const Point& point, window::MouseButton button)>
          mouse_button_down_function);

  // Tells the node the mouse has released button.
  void MouseButtonUp(const Point& point, window::MouseButton button);

  // Gets the nodes at the given point, ordered front to back, and calls
  // `on_hit_node` with the hit node, and where (relative to the node)
  // it hit. If this method returns true, nodes behind this node should not
  // be hit tested.
  bool GetNodesAt(
      const Point& point,
      const std::function<void(Node& node, const Point& point_in_node)>&
          on_hit_node);

  // Sets whether hit tests should be blocked from hitting anything behind this
  // node.
  void SetBlocksHitTest(bool blocks_hit_test);

  // Returns whether hit tests are blocked from hitting anything behind this
  // node.
  bool BlocksHitTest();

  // Adds a function to call when the node gains focus.
  void OnGainFocus(std::function<void()> gain_focus_function);

  // Tells the node it has gained focus.
  void GainFocus();

  // Adds a function to call when the node loses focus.
  void OnLoseFocus(std::function<void()> lose_focus_function);

  // Tells the node it has lost focus.
  void LoseFocus();

  // Adds a function to call when the node becomes invalidated.
  void OnInvalidate(std::function<void()> on_invalidate_function);

  // Notifies the node that it needs to be redrawn.
  void Invalidate();

  // Whether this node handles mouse leave events.
  bool DoesHandleMouseLeaveEvents();

  bool IsHidden();

  const Point& GetOffset() const;

  void SetOffset(const Point& offset);

  Rectangle GetAreaRelativeToParent();

  Point GetPositionRelativeToParent();

  Size GetSize();

  template <typename... Modifiers>
  void Apply(Modifiers... modifiers) {
    ([this](auto&& modifier) { ApplySingleModifier(modifier); }(modifiers),
     ...);
  }

 private:
  std::weak_ptr<Node> parent_;
  std::list<std::shared_ptr<Node>> children_;
  std::map<size_t, std::shared_ptr<void>> components_;

  bool invalidated_;
  YGNode* yoga_node_;
  bool invalidate_when_dirtied_;
  bool handles_mouse_events_;
  bool blocks_hit_test_;
  Point scroll_offset_;

  std::function<Size(float, YGMeasureMode, float, YGMeasureMode)>
      measure_function_;
  std::function<bool(const Point&, const Size&)> hit_test_function_;
  std::vector<std::function<void()>> on_invalidate_functions_;
  std::vector<std::function<void(const DrawContext&)>> on_draw_functions_;
  std::vector<std::function<void(const DrawContext&)>>
      on_draw_post_children_functions_;
  std::vector<std::function<void(const Point&)>> on_mouse_hover_functions_;
  std::vector<std::function<void()>> on_mouse_leave_functions_;
  std::vector<std::function<void(const Point&, window::MouseButton)>>
      on_mouse_button_down_functions_;
  std::vector<std::function<void(const Point&, window::MouseButton)>>
      on_mouse_button_up_functions_;
  std::vector<std::function<void()>> on_gain_focus_functions_;
  std::vector<std::function<void()>> on_lose_focus_functions_;

  void SetParent(std::weak_ptr<Node> parent);
  void DrawChildren(DrawContext& draw_context);

  void InvalidateWhenDirtied();
  static void LayoutDirtied(const YGNode* node);
  static YGSize Measure(const YGNode* node, float width,
                        YGMeasureMode width_mode, float height,
                        YGMeasureMode height_mode);

  // Templated struct for matching against the first argument type.
  template <typename T>
  struct FirstArgument;

  // Templated struct for matching against the first argument type.
  template <typename Ret, typename Arg, typename... Args>
  struct FirstArgument<std::function<Ret(Arg, Args...)>> {
    using type = Arg;
  };

  // Applies a single modifier that matches a node. In this case, it adds the
  // node as a child.
  void ApplySingleModifier(std::shared_ptr<Node> node) { AddChild(node); }

  // Applies a single modifier that matches a pointer to a node pointer. In this
  // case, it stores the current node in the pointer.
  void ApplySingleModifier(std::shared_ptr<Node>* node_ptr) {
    *node_ptr = this->shared_from_this();
  }

  // Applies a single modifier that matches a weak pointer to a node pointer. In
  // this case, it stores the current node in the pointer.
  void ApplySingleModifier(std::weak_ptr<Node>* node_ptr) {
    *node_ptr = this->shared_from_this();
  }

  // Applies a single modifier that matches a layout pointer.
  void ApplySingleModifier(std::shared_ptr<Layout>* layout_ptr) {
    // Not supported.
    layout_ptr->reset();
  }

  // Applies a single modifier that matches a weak layout pointer.
  void ApplySingleModifier(std::weak_ptr<Layout>* layout_ptr) {
    // Not supported.
    layout_ptr->reset();
  }

  // Applies a single modifier that matches a component pointer. In this case,
  // it stores the component instance in the pointer.
  template <typename C>
  void ApplySingleModifier(std::shared_ptr<C>* component_ptr) {
    *component_ptr = GetOrAdd<C>();
  }

  // Applies a single modifier that matches a weak component pointer. In this
  // case, it stores the component instance in the pointer.
  template <typename C>
  void ApplySingleModifier(std::weak_ptr<C>* component_ptr) {
    *component_ptr = GetOrAdd<C>();
  }

  // Applies a single modifier that should match a function that takes a
  // constant reference to a `Node`, `Layout`, or component. This is called if
  // the parameter doesn't match against the previous `ApplySingleModifier`s.
  template <typename F>
  void ApplySingleModifier(F&& function) {
    // Get the type of the first parameter without the reference.
    using ParameterType = std::remove_reference_t<
        typename FirstArgument<decltype(std::function{function})>::type>;

    if constexpr (std::is_same_v<ParameterType, Node>) {
      std::invoke(function, *this);
    } else if constexpr (std::is_same_v<ParameterType, Layout>) {
      auto layout = GetLayout();
      std::invoke(function, layout);
    } else {
      std::invoke(function, *GetOrAdd<ParameterType>());
    }
  }
};

}  // namespace ui
}  // namespace perception
