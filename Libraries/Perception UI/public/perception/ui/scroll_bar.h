// Copyright 2022 Google LLC
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

#include "perception/ui/label.h"
#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class ScrollBar : public Widget {
 public:
  enum class Direction {
    VERTICAL,
    HORIZONTAL
  };

  // Creates a standard scrollbar.
  static std::shared_ptr<ScrollBar> Create();

  virtual ~ScrollBar();

  ScrollBar* SetDirection(Direction direction);
  Direction GetDirection() const;

  ScrollBar* OnScroll(std::function<void(float)> on_scroll_handler);

  ScrollBar* SetPosition(float minimum, float maximum, float value,
    float size);

  float GetPosition() const;

  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) override;

  virtual void OnMouseEnter() override;
  virtual void OnMouseMove(float x, float y) override;
  virtual void OnMouseLeave() override;
  virtual void OnMouseButtonDown(
      float x, float y,
      ::permebuf::perception::devices::MouseButton button) override;
  virtual void OnMouseButtonUp(
      float x, float y,
      ::permebuf::perception::devices::MouseButton button) override;

 private:
  ScrollBar();

  void SetDirectionInternal(Direction direction);

  virtual void Draw(DrawContext& draw_context) override;

  void OffsetTrackToFabCoordinates(float &x, float &y, float &width, float &height);
  uint32 GetFabColor();

  Direction direction_;

  std::function<void(float)> on_scroll_handler_;
  bool is_mouse_hovering_over_track_;
  bool is_mouse_hovering_over_fab_;

  bool is_dragging_;
  float fab_drag_offset_;

  float minimum_;
  float maximum_;
  float value_;
  float size_;
};

}  // namespace ui
}  // namespace perception
