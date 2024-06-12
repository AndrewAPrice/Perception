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

namespace perception {
namespace window {

struct KeyboardKeyEvent;
struct WindowDrawBuffer;
struct MouseButtonEvent;
struct MouseClickEvent;
struct MouseHoverEvent;
struct MouseMoveEvent;
struct MouseScrollEvent;
struct Rectangle;
class Window;

// Delegate for `Window` to notify of events happening pertaining to the window.
class WindowDelegate {
 public:
  // Requests for the delegate to draw into the `buffer`. The `invalidated_area`
  // is mutable.
  virtual void WindowDraw(const WindowDrawBuffer& buffer,
                          Rectangle& invalidated_area) {}

  virtual void WindowClosed() {}

  virtual void WindowResized() {}

  virtual void WindowFocusChanged() {}

  virtual void MouseMoved(const MouseMoveEvent& event) {}

  virtual void MouseScrolled(const MouseScrollEvent& event) {}

  virtual void MouseButtonChanged(const MouseButtonEvent& event) {}

  virtual void MouseClicked(const MouseClickEvent& event) {}

  virtual void MouseEntered() {}

  virtual void MouseLeft() {}

  virtual void MouseHovered(const MouseHoverEvent& event) {}

  virtual void MouseCaptivityChanged() {}

  virtual void KeyPressed(const KeyboardKeyEvent& event) {}

  virtual void KeyReleased(const KeyboardKeyEvent& event) {}

  virtual void KeyboardCaptivityChanged() {}

  virtual void ToggledFullScreen(bool is_full_screen) {}
};

}  // namespace window
}  // namespace perception
