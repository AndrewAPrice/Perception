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

#include "window.h"

extern "C" {
#include <SDL_mouse.h>
#include <SDL_video.h>

#include "events/SDL_events_c.h"
}

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

#include "keyboard.h"
#include "nlohmann/json.hpp"
#include "perception/base64.h"
#include "perception/window/keyboard_key_event.h"
#include "perception/window/mouse_button_event.h"
#include "perception/window/mouse_click_event.h"
#include "perception/window/mouse_hover_event.h"
#include "perception/window/mouse_move_event.h"
#include "perception/window/mouse_scroll_event.h"
#include "perception/window/rectangle.h"
#include "perception/window/window.h"
#include "perception/window/window_draw_buffer.h"

PerceptionSDLWindow::PerceptionSDLWindow(SDL_Window* w) : sdl_win_(w) {}

void PerceptionSDLWindow::SetBaseWindow(
    std::shared_ptr<perception::window::Window> base_window) {
  base_window_ = base_window;
}

void PerceptionSDLWindow::KeyPressed(
    const perception::window::KeyboardKeyEvent& event) {
  SDL_SendKeyboardKey(SDL_PRESSED, SDLScanCodeForPerceptionKeyCode(event.key));
}

void PerceptionSDLWindow::KeyReleased(
    const perception::window::KeyboardKeyEvent& event) {
  SDL_SendKeyboardKey(SDL_RELEASED, SDLScanCodeForPerceptionKeyCode(event.key));
}

void PerceptionSDLWindow::MouseMoved(
    const perception::window::MouseMoveEvent& event) {
  SDL_SendMouseMotion(sdl_win_, 0, 1, static_cast<int>(event.delta_x),
                      static_cast<int>(event.delta_y));
}

void PerceptionSDLWindow::MouseScrolled(
    const perception::window::MouseScrollEvent& event) {
  SDL_SendMouseWheel(sdl_win_, 0, 0.0f, event.delta, SDL_MOUSEWHEEL_NORMAL);
}

void PerceptionSDLWindow::MouseButtonChanged(
    const perception::window::MouseButtonEvent& event) {
  int btn = SDL_BUTTON_LEFT;
  if (event.button == perception::window::MouseButton::Right)
    btn = SDL_BUTTON_RIGHT;
  if (event.button == perception::window::MouseButton::Middle)
    btn = SDL_BUTTON_MIDDLE;
  SDL_SendMouseButton(sdl_win_, 0,
                      event.is_pressed_down ? SDL_PRESSED : SDL_RELEASED, btn);
}

void PerceptionSDLWindow::MouseClicked(
    const perception::window::MouseClickEvent& event) {
  SDL_SendMouseMotion(sdl_win_, 0, 0, event.x, event.y);
  int btn = SDL_BUTTON_LEFT;
  if (event.button == perception::window::MouseButton::Right)
    btn = SDL_BUTTON_RIGHT;
  if (event.button == perception::window::MouseButton::Middle)
    btn = SDL_BUTTON_MIDDLE;
  SDL_SendMouseButton(sdl_win_, 0,
                      event.was_pressed_down ? SDL_PRESSED : SDL_RELEASED, btn);
}

void PerceptionSDLWindow::MouseHovered(
    const perception::window::MouseHoverEvent& event) {
  SDL_SendMouseMotion(sdl_win_, 0, 0, event.x, event.y);
}

void PerceptionSDLWindow::WindowClosed() {
  SDL_SendWindowEvent(sdl_win_, SDL_WINDOWEVENT_CLOSE, 0, 0);
}

void PerceptionSDLWindow::WindowResized() {
  if (base_window_) {
    SDL_SendWindowEvent(sdl_win_, SDL_WINDOWEVENT_RESIZED,
                        base_window_->GetWidth(), base_window_->GetHeight());
  }
}

void PerceptionSDLWindow::WindowFocusChanged() {
  if (base_window_) {
    if (base_window_->IsFocused()) {
      SDL_SendWindowEvent(sdl_win_, SDL_WINDOWEVENT_FOCUS_GAINED, 0, 0);
    } else {
      SDL_SendWindowEvent(sdl_win_, SDL_WINDOWEVENT_FOCUS_LOST, 0, 0);
    }
  }
}

void PerceptionSDLWindow::WindowDraw(
    const perception::window::WindowDrawBuffer& buffer,
    perception::window::Rectangle& invalidated_area) {
  std::scoped_lock lock(mutex_);
  if (!surface_ || !surface_->pixels || !buffer.pixel_data) return;
  int copy_w = std::min(buffer.width, surface_->w);
  int copy_h = std::min(buffer.height, surface_->h);
  if (copy_w <= 0 || copy_h <= 0) return;

  int min_y = std::max(0, invalidated_area.min_y);
  int max_y = std::min(copy_h, invalidated_area.max_y);
  int min_x = std::max(0, invalidated_area.min_x);
  int max_x = std::min(copy_w, invalidated_area.max_x);

  if (max_y <= min_y || max_x <= min_x) return;

  int width_to_copy = max_x - min_x;
  if (width_to_copy == buffer.width && width_to_copy == surface_->w &&
      buffer.width * 4 == surface_->pitch && min_x == 0) {
    memcpy(static_cast<uint8_t*>(buffer.pixel_data) +
               static_cast<size_t>(min_y) * buffer.width * 4,
           static_cast<uint8_t*>(surface_->pixels) +
               static_cast<size_t>(min_y) * surface_->pitch,
           static_cast<size_t>(max_y - min_y) * buffer.width * 4);
  } else {
    for (int y = min_y; y < max_y; ++y) {
      memcpy(static_cast<uint8_t*>(buffer.pixel_data) +
                 (static_cast<size_t>(y) * buffer.width + min_x) * 4,
             static_cast<uint8_t*>(surface_->pixels) +
                 static_cast<size_t>(y) * surface_->pitch + min_x * 4,
             static_cast<size_t>(width_to_copy) * 4);
    }
  }
}

perception::window::DebugUiHierarchy PerceptionSDLWindow::GetUiHierarchy() {
  std::scoped_lock lock(mutex_);
  perception::window::DebugUiHierarchy hierarchy;

  int width = 0;
  int height = 0;
  if (surface_) {
    width = surface_->w;
    height = surface_->h;
  } else if (base_window_) {
    width = base_window_->GetWidth();
    height = base_window_->GetHeight();
  }

  std::string title = "Perception SDL App";
  if (sdl_win_) {
    const char* win_title = SDL_GetWindowTitle(sdl_win_);
    if (win_title && *win_title) {
      title = win_title;
    }
  }

  std::string texture_b64;
  if (surface_ && surface_->pixels && width > 0 && height > 0) {
    std::vector<uint8_t> rgba(width * height * 4);
    for (int y = 0; y < height; ++y) {
      const uint8_t* src_row =
          static_cast<const uint8_t*>(surface_->pixels) + y * surface_->pitch;
      uint8_t* dst_row = rgba.data() + y * width * 4;
      for (int x = 0; x < width; ++x) {
        dst_row[x * 4 + 0] = src_row[x * 4 + 2];
        dst_row[x * 4 + 1] = src_row[x * 4 + 1];
        dst_row[x * 4 + 2] = src_row[x * 4 + 0];
        dst_row[x * 4 + 3] = src_row[x * 4 + 3];
      }
    }
    texture_b64 = perception::Base64Encode(rgba.data(), rgba.size());
  }

  ::nlohmann::json j;
  std::stringstream id_ss;
  id_ss << "0x" << std::hex << reinterpret_cast<uintptr_t>(this);
  j["id"] = id_ss.str();
  j["window_title"] = title;
  j["hidden"] = false;
  j["calculated_layout"] = {
      {"left", 0}, {"top", 0}, {"width", width}, {"height", height}};

  if (!texture_b64.empty()) {
    j["texture"] = texture_b64;
  }

  j["handlers"] = ::nlohmann::json::array();
  j["components"] = {"SDLWindow"};
  j["component_properties"] = ::nlohmann::json::object();
  j["layout"] = ::nlohmann::json::object();
  j["children"] = ::nlohmann::json::array();

  hierarchy.json = j.dump();
  return hierarchy;
}
