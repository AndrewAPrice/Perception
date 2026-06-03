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
#include <string>
#include <vector>

#include "include/core/SkFont.h"
#include "perception/type_id.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/node.h"
#include "yoga/Yoga.h"

namespace perception {
namespace ui {
namespace components {

class InputBox : public UniqueIdentifiableType<InputBox> {
 public:
  // Creates a basic, single line, input box.
  template <typename... Modifiers>
  static std::shared_ptr<Node> BasicInputBox(std::string_view text,
                                             Modifiers... modifiers) {
    return Node::Empty([text](InputBox& input_box) { input_box.SetText(text); },
                       modifiers...);
  }

  InputBox();

  void SetNode(std::weak_ptr<Node> node);

  void SetText(std::string_view text);
  std::string GetText() const;
  bool HasFocus() const;

  // Callback to use when the text has changed.
  void OnTextChanged(std::function<void(std::string_view)> handler);
  void OnEnterPressed(std::function<void(std::string_view)> handler);

  void SetFont(SkFont* font);
  SkFont* GetFont() const;

  void SetTextColor(uint32 color);
  uint32 GetTextColor() const;

 private:
  std::weak_ptr<Node> node_;
  std::shared_ptr<Focusable> focusable_;
  SkFont* font_;
  std::string text_;
  size_t cursor_index_;
  float scroll_x_;
  bool shift_pressed_;
  bool is_hovering_;
  bool is_pushed_;
  uint32 text_color_;

  std::vector<std::function<void(std::string_view)>> on_text_changed_handlers_;
  std::vector<std::function<void(std::string_view)>> on_enter_pressed_handlers_;

  void Draw(const DrawContext& draw_context);
  Size Measure(float width, YGMeasureMode width_mode, float height,
               YGMeasureMode height_mode);

  void MoveCursorToPoint(const Point& point);
  void EnsureCursorVisible();
  void HandleKeyDown(const window::KeyboardKeyEvent& event);
  void HandleKeyUp(const window::KeyboardKeyEvent& event);

  void NotifyTextChanged();
  void NotifyEnterPressed();
  void AssignDefaultFontIfUnassigned();
};

}  // namespace components
}  // namespace ui

extern template class UniqueIdentifiableType<ui::components::InputBox>;

}  // namespace perception
