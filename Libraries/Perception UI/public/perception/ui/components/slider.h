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
#include <vector>

#include "perception/type_id.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {
namespace components {

class Slider : public UniqueIdentifiableType<Slider> {
 public:
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicSlider(float minimum, float maximum,
                                           float value,
                                           std::function<void(float)> on_change,
                                           Modifiers... modifiers) {
    return Node::Empty(
        [](Layout& layout) {
          layout.SetHeight(kSliderHeight);
          layout.SetMinWidth(kSliderMinWidth);
        },
        [minimum, maximum, value, on_change](Slider& slider) {
          slider.SetRange(minimum, maximum);
          slider.SetValue(value);
          slider.OnChange(on_change);
        },
        modifiers...);
  }

  Slider();

  void SetNode(std::weak_ptr<Node> node);

  void SetRange(float minimum, float maximum);
  void SetValue(float value);
  float GetValue() const;

  void OnChange(std::function<void(float)> on_change);

 private:
  float minimum_;
  float maximum_;
  float value_;
  bool is_dragging_;

  std::vector<std::function<void(float)>> on_change_handlers_;
  std::weak_ptr<Node> node_;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);

  void MouseHover(const Point& point);
  void MouseLeave();
  void MouseButtonDown(const Point& point, window::MouseButton button);
  void MouseButtonUp(const Point& point, window::MouseButton button);

  float CalculateValueFromX(float x, float width);
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::Slider>;

}  // namespace perception
