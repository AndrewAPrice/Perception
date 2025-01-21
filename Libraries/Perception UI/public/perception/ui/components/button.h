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
#include "perception/ui/components/label.h"
#include "perception/ui/node.h"
#include "perception/window/mouse_button.h"

namespace perception {
namespace ui {
struct Point;

namespace components {

class Button {
 public:
  // Creates a basic button. There's nothing inside the button, so the
  // caller can add their own child nodes to display custom content inside
  // the button.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicButton(std::function<void()> on_push,
                                           Modifiers... modifiers) {
    return Node::Empty(
        [](Layout& layout) {
          layout.SetMinWidth(24.0f);
          layout.SetMinHeight(24.0f);
          layout.SetAlignItems(YGAlignCenter);
          layout.SetJustifyContent(YGJustifyCenter);
        },
        [](Block& block) {
          block.SetBorderRadius(4.0f);
          block.SetBorderWidth(1.0f);
          block.SetBorderColor(0xFF000000);
        },
        [&on_push](Button& button) { button.OnPush(on_push); }, modifiers...);
  }

  // Creates a text button.
  template <typename... Modifiers>
  static std::shared_ptr<Node> TextButton(std::string_view text,
                                          std::function<void()> on_push,
                                          Modifiers... modifiers) {
    return BasicButton(on_push, Label::BasicLabel(text), modifiers...);
  }

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
