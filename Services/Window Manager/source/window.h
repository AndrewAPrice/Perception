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
#include <optional>
#include <string>
#include <string_view>

#include "mouse.h"
#include "perception/devices/keyboard_listener.h"
#include "perception/devices/mouse_listener.h"
#include "perception/linked_list.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/size.h"
#include "perception/window/base_window.h"
#include "perception/window/window_manager.h"
#include "status.h"
#include "types.h"
#include "window_buttons.h"

namespace perception {
class Font;
}

class Frame;

class Window : public std::enable_shared_from_this<Window> {
 public:
  static StatusOr<std::shared_ptr<Window>> CreateWindow(
      const ::perception::window::CreateWindowRequest& request);

  ~Window();

  void Focus();
  bool IsFocused() const;
  void Close();
  static void UnfocusAllWindows();
  std::string_view GetTitle() const;

  static bool ForEachFrontToBackWindow(
      const std::function<bool(Window&)>& on_each_window);
  static bool ForEachBackToFrontWindow(
      const std::function<bool(Window&)>& on_each_window);
  static Window* GetWindowBeingDragged();
  static void MouseNotHoveringOverWindowContents();
  bool MouseEvent(const ::perception::ui::Point& point,
                  std::optional<MouseButtonEvent> button_event);

  void Draw(const ::perception::ui::Rectangle& screen_area);
  void Invalidate();
  void Invalidate(const ::perception::ui::Rectangle& screen_area);
  void InvalidateLocalArea(const ::perception::ui::Rectangle& window_area);

  ::perception::ui::Rectangle GetScreenAreaWithFrame() const;
  const ::perception::ui::Rectangle& GetScreenArea() const;
  bool IsVisible() const { return is_visible_; }

  void SetTextureId(int texture_id);

  // Next/previous windows in the Z-order of things. Public for visibility from
  // LinkedList.
  ::perception::LinkedListNode z_ordering_linked_list_node_;

 private:
  void CommonInit();
  void Show();
  void Hide();
  void Resized();

  void Unfocus();
  bool IsDragging() const;
  bool IsHovering() const;

  ::perception::ui::Rectangle WindowButtonScreenArea() const;
  bool AreWindowButtonsVisible() const;
  void HandleWindowButtonClick();

  // The window's title.
  std::string title_;

  // The window's position.
  ::perception::ui::Rectangle screen_area_;

  // Whether the window is visible. This also corrolates to whether
  bool is_visible_;

  bool is_resizable_;

  // The window button the mouse is over.
  std::optional<WindowButton> hovered_window_button_;

  bool hide_window_buttons_;

  // The texture representing the contents of this window.
  // 0 if unknown.
  size_t texture_id_;

  ::perception::window::BaseWindow::Client window_listener_;
  ::perception::MessageId message_id_to_notify_on_window_disappearence_;
  bool window_listener_already_disappeared_;

  ::perception::devices::KeyboardListener::Client keyboard_listener_;

  ::perception::devices::MouseListener::Client mouse_listener_;
};

std::shared_ptr<Window> GetWindowWithListener(
    const ::perception::window::BaseWindow::Client& window_listener);

// ::perception::Font* GetWindowTitleFont();
