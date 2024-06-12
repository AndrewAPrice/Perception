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
#include <memory>
#include <string>
#include <string_view>

#include "perception/ui/container.h"
#include "perception/ui/label.h"
#include "perception/ui/widget.h"
#include "perception/window/mouse_button.h"

namespace perception {
namespace ui {

struct DrawContext;

class Button : public Container {
 public:
  // Creates a standard button with a text label.
  static std::shared_ptr<Button> Create();

  // Creates a blank button that you can add your own widgets to as content.
  static std::shared_ptr<Button> CreateCustom();

  virtual ~Button();

  Button* OnClick(std::function<void()> on_click_handler);

  // Sets the label of the button. Does nothing if this is a custom button.
  Button* SetLabel(std::string_view label);
  // Returns the label of the button. Returns a blank string if this is a custom
  // button.
  std::string_view GetLabel();

  // Sets the text color of the button. Does nothing if this is a custom button.
  Button* SetTextColor(uint32 color);
  // Returns the text color of the button. Returns a transparency if this is a
  // custom button.
  uint32 GetTextColor();

  // Sets the background color of the button.
  Button* SetUnpushedBackgroundColor(uint32 color);
  // Returns the background color of the button.
  uint32 GetUnpushedBackgroundColor();

  // Sets the background color when hovered over.
  Button* SetBackgroundHoverColor(uint32 color);
  // Returns the background color when hovered over.
  uint32 GetBackgroundHoverColor(uint32 color);

  // Sets the background color when pushed.
  Button* SetBackgroundPushedColor(uint32 color);
  // Returns the background color when pushed.
  uint32 GetBackgroundPushedColor(uint32 color);

  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) override;

  virtual void OnMouseEnter() override;
  virtual void OnMouseLeave() override;
  virtual void OnMouseButtonDown(float x, float y,
                                 window::MouseButton button) override;
  virtual void OnMouseButtonUp(float x, float y,
                               window::MouseButton button) override;

 protected:
  Button();
  void ApplyBackgroundColor();

  std::shared_ptr<Label> label_;

  std::function<void()> on_click_handler_;
  bool is_pushed_down_;
  bool is_mouse_hovering_;

  uint32 unpushed_background_color_;
  uint32 background_hover_color_;
  uint32 background_pushed_color_;
};

}  // namespace ui
}  // namespace perception
