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
using ::perception::Status;
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
int mouse_texture_id;

constexpr uint32 kMousePointer[] = {
    0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0xFF000000,
    0xFF000000, 0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000,
    0xFF000000, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0xFF000000,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0xFF000000, 0xFF000000,
    0x00000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFC3C3C3, 0xFFC3C3C3,
    0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0xFF000000, 0xFF000000, 0xFF000000, 0x00000000,
    0x00000000};

constexpr Size kMousePointerSize = {11, 17};

Rectangle MouseBounds() {
  return Rectangle{.origin = mouse_position, .size = kMousePointerSize};
}

class MyMouseListener : public MouseListener::Server {
 public:
  Status MouseMove(const RelativeMousePositionEvent& message) override {
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
      // Invalidate the area under the cursor.
      Rectangle new_mouse_area{.origin = mouse_position,
                               .size = kMousePointerSize};
      Rectangle old_mouse_area{.origin = old_mouse_position,
                               .size = kMousePointerSize};
      InvalidateScreen(new_mouse_area.Union(old_mouse_area));

      // Test if any of the dialogs (from front to back) can handle this
      // click.
      (void)Window::ForEachFrontToBackWindow([](Window& window) {
        return window.MouseEvent(mouse_position, std::nullopt);
      });
    }

    return Status::OK;
  }

  Status MouseButton(
      const ::perception::devices::MouseButtonEvent& message) override {
    std::optional<MouseButtonEvent> mouse_button_event = MouseButtonEvent{
        .button = message.button, .is_pressed_down = message.is_pressed_down};
    // Test if any of the dialogs (from front to back) can handle this
    // click.
    if (Window::ForEachFrontToBackWindow([mouse_button_event](Window& window) {
          return window.MouseEvent(mouse_position, mouse_button_event);
        }))
      return Status::OK;

    Window::UnfocusAllWindows();
    return Status::OK;
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

  // Create a texture for the mouse.
  graphics::CreateTextureRequest create_texture_request;
  create_texture_request.size.width = kMousePointerSize.width;
  create_texture_request.size.height = kMousePointerSize.height;

  auto create_texture_response =
      GetService<GraphicsDevice>().CreateTexture(create_texture_request);
  mouse_texture_id = create_texture_response->texture.id;
  create_texture_response->pixel_buffer->Apply([](void* data, size_t) {
    uint32* destination = (uint32*)data;
    for (int i = 0; i < kMousePointerSize.width * kMousePointerSize.height;
         i++) {
      destination[i] = kMousePointer[i];
    }
  });
}

const Point& GetMousePosition() { return mouse_position; }

void DrawMouse(const Rectangle& draw_area) {
  auto mouse_bounds = MouseBounds();
  if (!draw_area.Intersects(mouse_bounds)) {
    // The mouse is outside of the draw area.
    return;
  }

  CopyAlphaBlendedTexture(mouse_bounds, mouse_texture_id, {0.0f, 0.0f});
}