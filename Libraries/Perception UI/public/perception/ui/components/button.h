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
#include <string>
#include <string_view>

#include "perception/ui/components/block.h"
#include "perception/ui/node.h"
#include "perception/window/mouse_button.h"

namespace perception {
namespace ui {
struct Point;

namespace components {

class Button {
 public:
  Button();
  void SetNode(std::weak_ptr<Node> node);

  void SetIdleColor(uint32 color);
  uint32 GetIdleColor() const;

  void SetHoverColor(uint32 color);
  uint32 GetHoverColor() const;

  void SetPushedColor(uint32 color);
  uint32 GetPushedColor() const;

  void OnPush(std::function<void()> on_push);

 private:
    uint32 idle_color_;
    uint32 hover_color_;
    uint32 pushed_color_;

    bool is_hovering_;
    bool is_pushed_;
    std::vector<std::function<void()>> on_push_;
    
    std::weak_ptr<components::Block> block_;

    void UpdateFillColor();
    uint32 GetFillColor();

    void MouseHover(const Point& point);
    void MouseLeave();
    void MouseButtonDown(const Point& point, window::MouseButton button);
    void MouseButtonUp(const Point& point, window::MouseButton button);

};

}  // namespace components
}  // namespace ui
}  // namespace perception
