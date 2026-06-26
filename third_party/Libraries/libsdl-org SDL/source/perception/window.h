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

#include <memory>
#include <mutex>

#include "perception/window/window_delegate.h"

struct SDL_Window;
struct SDL_Surface;

namespace perception {
namespace window {
class Window;
struct KeyboardKeyEvent;
struct MouseButtonEvent;
struct MouseClickEvent;
struct MouseHoverEvent;
struct MouseMoveEvent;
struct MouseScrollEvent;
struct Rectangle;
struct WindowDrawBuffer;
}  // namespace window
}  // namespace perception

class PerceptionSDLWindow
    : public perception::window::WindowDelegate,
      public std::enable_shared_from_this<PerceptionSDLWindow> {
 public:
  SDL_Window* sdl_win_;
  SDL_Surface* surface_ = nullptr;
  std::shared_ptr<perception::window::Window> base_window_;
  std::mutex mutex_;

  PerceptionSDLWindow(SDL_Window* w);

  void SetBaseWindow(std::shared_ptr<perception::window::Window> base_window);

  void KeyPressed(const perception::window::KeyboardKeyEvent& event) override;
  void KeyReleased(const perception::window::KeyboardKeyEvent& event) override;

  void MouseMoved(const perception::window::MouseMoveEvent& event) override;

  void MouseScrolled(
      const perception::window::MouseScrollEvent& event) override;

  void MouseButtonChanged(
      const perception::window::MouseButtonEvent& event) override;

  void MouseClicked(
      const perception::window::MouseClickEvent& event) override;

  void MouseHovered(const perception::window::MouseHoverEvent& event) override;

  void WindowClosed() override;

  void WindowResized() override;

  void WindowFocusChanged() override;

  void WindowDraw(const perception::window::WindowDrawBuffer& buffer,
                  perception::window::Rectangle& invalidated_area) override;

  perception::window::DebugUiHierarchy GetUiHierarchy() override;
};
