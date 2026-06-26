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

#include "mouse.h"

#include <memory>

#include "compositor.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/services.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/size.h"
#include "screen.h"
#include "status.h"
#include "window.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::GetService;
using ::perception::MessageId;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::ProcessId;
using ::perception::devices::GraphicsDevice;
using ::perception::devices::MouseButton;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseDevice;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::ui::Point;
using ::perception::ui::Rectangle;
using ::perception::ui::Size;

namespace {

Point mouse_position;
Rectangle last_mouse_bounds;
std::weak_ptr<Window> pressed_window;

const char* kPointerSprite =
    "BB.........\n"
    "BGB........\n"
    "BWGB.......\n"
    "BWWGB......\n"
    "BWWWGB.....\n"
    "BWWWWGB....\n"
    "BWWWWWGB...\n"
    "BWWWWWWGB..\n"
    "BWWWWWWWGB.\n"
    "BWWWWWWWWGB\n"
    "BWWWWWBBBBB\n"
    "BWWBWWB....\n"
    "BWB.BWWB...\n"
    "BB..BWWB...\n"
    ".....BBBB..\n"
    "...........\n"
    "...........";

const char* kPokeSprite =
    "...BBB.......\n"
    "...BWGB......\n"
    "...BWWGB.....\n"
    "...BWWGBBB...\n"
    "...BWWGBWGBBB\n"
    ".BBBWWGBWGBWB\n"
    "BWGBWWGBWGBWB\n"
    "BWWWWWGBWGBWB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    ".BWWWWWWWWGB.\n"
    "..BBBBBBBB...";

const char* kDragSprite =
    "...BBBGBBB...\n"
    "...BWWGBWGBBB\n"
    ".BBBWWGBWGBWB\n"
    "BWGBWWGBWGBWB\n"
    "BWWWWWGBWGBWB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    "BWWWWWWWWWWGB\n"
    ".BWWWWWWWWGB.\n"
    "..BBBBBBBB...";

const char* kResizeVerticalSprite =
    ".....B.....\n"
    "....BWB....\n"
    "...BWWWB...\n"
    "..BWWWWWB..\n"
    ".BWWWWWWWB.\n"
    "BWWWWWWWWWB\n"
    "BBBBWWWBBBB\n"
    "...BWWWB...\n"
    "...BWWWB...\n"
    "...BWWWB...\n"
    "...BWWWB...\n"
    "...BWWWB...\n"
    "BBBBWWWBBBB\n"
    "BWWWWWWWWWB\n"
    ".BWWWWWWWB.\n"
    "..BWWWWWB..\n"
    "...BWWWB...\n"
    "....BWB....\n"
    ".....B....";

const char* kResizeHorizontalSprite =
    ".....BB.....BB.....\n"
    "....BWB.....BWB....\n"
    "...BWWB.....BWWB...\n"
    "..BWWWB.....BWWWB..\n"
    ".BWWWWBBBBBBBWWWWB.\n"
    "BWWWWWWWWWWWWWWWWWB\n"
    ".BWWWWWWWWWWWWWWWB.\n"
    "..BWWWBBBBBBBWWWB..\n"
    "...BWWB.....BWWB...\n"
    "....BWB.....BWB....\n"
    ".....BB.....BB.....";

const char* kResizeDiagonalTopLeftBottomRightSprite =
    "BBBBBBBBB....\n"
    "BWWWWWWB.....\n"
    "BWWWWWB......\n"
    "BWWWWBB......\n"
    "BWWWWWB.....B\n"
    "BWWBWWWB...BB\n"
    "BWB.BWWWB.BWB\n"
    "BB...BWWWWWWB\n"
    "B.....BWWWWWB\n"
    ".......BWWWWB\n"
    "......BWWWWWB\n"
    ".....BWWWWWWB\n"
    "....BBBBBBBBB";

const char* kResizeDiagonalTopRightBottomLeftSprite =
    "....BBBBBBBBB\n"
    ".....BWWWWWWB\n"
    "......BWWWWWB\n"
    ".......BWWWWB\n"
    "B.....BWWWWWB\n"
    "BB...BWWWBWWB\n"
    "BWB.BWWWB.BWB\n"
    "BWWBWWWB...BB\n"
    "BWWWWWB.....B\n"
    "BWWWWB.......\n"
    "BWWWWWB......\n"
    "BWWWWWWB.....\n"
    "BBBBBBBBB....";

const char* kCaretSprite =
    "BBBBBBB\n"
    "BWWWWWB\n"
    "BBBWBBB\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "..BWB..\n"
    "BBBWBBB\n"
    "BWWWWWB\n"
    "BBBBBBB";

struct CursorDef {
  const char* sprite;
  Size size;
  Point hotspot;
  int texture_id;
};

CursorDef cursor_defs[] = {
    {kPointerSprite, {11, 17}, {0, 0}, 0},
    {kPokeSprite, {13, 15}, {5, 1}, 0},
    {kDragSprite, {13, 12}, {6, 6}, 0},
    {kResizeHorizontalSprite, {19, 11}, {9, 5}, 0},
    {kResizeVerticalSprite, {11, 19}, {5, 9}, 0},
    {kResizeDiagonalTopLeftBottomRightSprite, {13, 13}, {6, 6}, 0},
    {kResizeDiagonalTopRightBottomLeftSprite, {13, 13}, {6, 6}, 0},
    {kCaretSprite, {7, 17}, {3, 8}, 0},
    {"", {0, 0}, {0, 0}, 0}};

void LoadSprite(std::string_view sprite, int width, int height,
                uint32* destination) {
  int x = 0, y = 0;
  for (char c : sprite) {
    if (c == '\n') {
      x = 0;
      y++;
      continue;
    }
    if (x < width && y < height) {
      uint32 color = 0x00000000;  // transparent
      if (c == 'B')
        color = 0xFF000000;  // Black border
      else if (c == 'W')
        color = 0xFFFFFFFF;  // White fill
      else if (c == 'G')
        color = 0xFFC3C3C3;  // Gray highlight/shadow
      destination[y * width + x] = color;
      x++;
    }
  }
}

Rectangle MouseBounds() {
  auto cursor_type = Window::GetCursorAtPoint(mouse_position);
  int idx = static_cast<int>(cursor_type);
  if (idx < 0 || idx >= 9) idx = 0;
  const auto& def = cursor_defs[idx];
  return Rectangle{.origin = mouse_position - def.hotspot, .size = def.size};
}

class MyMouseListener : public MouseListener::Server {
 public:
  Status MouseMove(const RelativeMousePositionEvent& message) override {
    if (auto captive_win = Window::GetCaptiveMouseWindow()) {
      if (captive_win->IsVisible() && captive_win->IsFocused()) {
        captive_win->GetMouseListener().MouseMove(message, nullptr);
        return Status::OK;
      }
    }

    auto old_mouse_position = mouse_position;

    mouse_position.x += static_cast<int>(message.delta_x);
    mouse_position.y += static_cast<int>(message.delta_y);

    auto screen_size = GetScreenSize();
    for (int i = 0; i < 2; i++) {
      mouse_position[i] =
          std::max(0.0f, std::min(mouse_position[i], screen_size[i] - 1));
    }

    // Has the mouse moved?
    if (old_mouse_position != mouse_position) {
      // Test if any of the dialogs (from front to back) can handle this
      // click.
      (void)Window::ForEachFrontToBackWindow([](Window& window) {
        return window.MouseEvent(mouse_position, std::nullopt);
      });

      InvalidateMouse();
    }

    return Status::OK;
  }

  Status MouseButton(
      const ::perception::devices::MouseButtonEvent& message) override {
    if (auto captive_win = Window::GetCaptiveMouseWindow()) {
      if (captive_win->IsVisible() && captive_win->IsFocused()) {
        captive_win->GetMouseListener().MouseButton(message, nullptr);
        return Status::OK;
      }
    }

    std::optional<MouseButtonEvent> mouse_button_event = MouseButtonEvent{
        .button = message.button, .is_pressed_down = message.is_pressed_down};
    if (message.is_pressed_down) {
      if (Window::ForEachFrontToBackWindow(
              [mouse_button_event](Window& window) {
                if (window.MouseEvent(mouse_position, mouse_button_event)) {
                  pressed_window = window.weak_from_this();
                  return true;
                }
                return false;
              })) {
        return Status::OK;
      }
      pressed_window.reset();
      Window::UnfocusAllWindows();
      return Status::OK;
    } else {
      if (auto strong_window = pressed_window.lock()) {
        strong_window->MouseEvent(mouse_position, mouse_button_event);
      }
      pressed_window.reset();
      (void)Window::ForEachFrontToBackWindow([](Window& window) {
        return window.MouseEvent(mouse_position, std::nullopt);
      });
      return Status::OK;
    }
  }
};

std::unique_ptr<MyMouseListener> mouse_listener;

}  // namespace

void InitializeMouse() {
  mouse_position = GetScreenSize().ToPoint();
  for (int i = 0; i < 2; i++) mouse_position[i] /= 2.0f;

  mouse_listener = std::make_unique<MyMouseListener>();

  // Tell each mouse driver who we are.
  NotifyOnEachNewServiceInstance<MouseDevice>(
      [](MouseDevice::Client mouse_device) {
        // Tell the mouse driver to send us mouse messages.
        mouse_device.SetMouseListener(*mouse_listener);
      });

  // Create a texture for each cursor.
  for (int i = 0; i < 9; i++) {
    auto& def = cursor_defs[i];
    if (def.size.width <= 0 || def.size.height <= 0) {
      def.texture_id = 0;
      continue;
    }
    graphics::CreateTextureRequest create_texture_request;
    create_texture_request.size.width = def.size.width;
    create_texture_request.size.height = def.size.height;

    auto create_texture_response =
        GetService<GraphicsDevice>().CreateTexture(create_texture_request);
    def.texture_id = create_texture_response->texture.id;
    create_texture_response->pixel_buffer->Apply([&def](void* data, size_t) {
      uint32* destination = (uint32*)data;
      LoadSprite(def.sprite, def.size.width, def.size.height, destination);
    });
  }

  last_mouse_bounds = MouseBounds();
}

const Point& GetMousePosition() { return mouse_position; }

void DrawMouse(const Rectangle& draw_area) {
  if (Window::GetCaptiveMouseWindow() != nullptr) return;

  auto mouse_bounds = MouseBounds();
  auto intersection = draw_area.Intersection(mouse_bounds);
  if (!intersection) {
    // The mouse is outside of the draw area.
    return;
  }

  auto cursor_type = Window::GetCursorAtPoint(mouse_position);
  int idx = static_cast<int>(cursor_type);
  if (idx < 0 || idx >= 9) idx = 0;
  const auto& def = cursor_defs[idx];
  if (def.texture_id == 0 || def.size.width <= 0 || def.size.height <= 0) {
    last_mouse_bounds = mouse_bounds;
    return;
  }

  Point offset = intersection->origin - mouse_bounds.origin;
  CopyAlphaBlendedTexture(*intersection, def.texture_id, offset);

  last_mouse_bounds = mouse_bounds;
}

void InvalidateMouse() {
  auto new_bounds = MouseBounds();
  InvalidateScreen(last_mouse_bounds.Union(new_bounds));
  last_mouse_bounds = new_bounds;
}