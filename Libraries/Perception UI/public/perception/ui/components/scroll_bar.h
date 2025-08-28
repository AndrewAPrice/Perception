// Copyright 2024 Google LLC
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

#include "include/core/SkFont.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "perception/type_id.h"

namespace perception {
namespace ui {

struct DrawContext;
struct Rectangle;

namespace components {

// A bar that controls a scrollable area.
class ScrollBar : public UniqueIdentifiableType<ScrollBar> {
 public:
  enum class Direction { VERTICAL, HORIZONTAL };

  ScrollBar();

  void SetNode(std::weak_ptr<Node> node);

  std::shared_ptr<Node> GetFab();

  void SetDirection(Direction direction);
  Direction GetDirection() const;

  void SetAlwaysShowScrollBar(bool always_show);
  bool AlwaysShowScrollBar() const;

  void OnScroll(std::function<void(float)> on_scroll_handler);

  void SetValue(float minimum, float maximum, float value, float size);
  float GetValue() const;

 private:
  Direction direction_;

  std::weak_ptr<Node> node_;
  std::shared_ptr<Node> fab_;

  std::vector<std::function<void(float)>> on_scroll_handlers_;
  bool is_mouse_hovering_over_track_;
  bool is_mouse_hovering_over_fab_;
  bool always_show_;

  bool is_dragging_;
  float fab_drag_offset_;

  float minimum_;
  float maximum_;
  float value_;
  float size_;

  uint32 GetFabColor() const;

  std::pair<float, float> CalculateFabOffsetAndSize(
      float available_length) const;

  float CalculateDragPosition(float mouse_offset, float fab_length,
                              float track_length);

  void AdjustRectangleForFab(Rectangle& rectangle) const;

  Rectangle GetFabArea() const;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);

  void MouseHover(const Point& point);
  void MouseLeave();
  void MouseButtonDown(const Point& point, window::MouseButton button);
  void MouseButtonUp(const Point& point, window::MouseButton button);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::ScrollBar>;

}  // namespace perception