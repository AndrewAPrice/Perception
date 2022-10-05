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
#include <string>
#include <string_view>

#include "perception/ui/widget.h"

namespace perception {
namespace ui {

struct DrawContext;

class Button : public Widget {
 public:
  Button();
  virtual ~Button();

  Button* OnClick(std::function<void()> on_click_handler);

  virtual bool GetWidgetAt(float x, float y, std::shared_ptr<Widget>& widget,
                           float& x_in_selected_widget,
                           float& y_in_selected_widget) override;

  virtual void OnMouseLeave() override;
  virtual void OnMouseButtonDown(
      float x, float y,
      ::permebuf::perception::devices::MouseButton button) override;
  virtual void OnMouseButtonUp(
      float x, float y,
      ::permebuf::perception::devices::MouseButton button) override;

 private:
  virtual void Draw(DrawContext& draw_context) override;

  std::function<void()> on_click_handler_;
  bool is_pushed_down_;
};

}  // namespace ui
}  // namespace perception
