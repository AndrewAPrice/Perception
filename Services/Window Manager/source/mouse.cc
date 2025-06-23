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
#include "frame.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/services.h"
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
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseClickEvent;
using ::perception::devices::MouseDevice;
using ::perception::devices::MouseListener;
using ::perception::devices::MousePositionEvent;
using ::perception::devices::RelativeMousePositionEvent;

namespace {

int mouse_x;
int mouse_y;
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

constexpr int kMousePointerWidth = 11;
constexpr int kMousePointerHeight = 17;

class MyMouseListener : public MouseListener::Server {
 public:
  Status MouseMove(const RelativeMousePositionEvent& message) override {
    int old_mouse_x = mouse_x;
    int old_mouse_y = mouse_y;

    mouse_x += static_cast<int>(message.delta_x);
    mouse_y += static_cast<int>(message.delta_y);

    mouse_x = std::max(0, std::min(mouse_x, GetScreenWidth() - 1));
    mouse_y = std::max(0, std::min(mouse_y, GetScreenHeight() - 1));

    // Has the mouse moved?
    if (mouse_x != old_mouse_x || mouse_y != old_mouse_y) {
      // Invalidate the area under the cursor.
      InvalidateScreen(std::min(mouse_x, old_mouse_x),
                       std::min(mouse_y, old_mouse_y),
                       std::max(mouse_x, old_mouse_x) + 11,
                       std::max(mouse_y, old_mouse_y) + 17);

      Window* dragging_window = Window::GetWindowBeingDragged();
      if (dragging_window) {
        dragging_window->DraggedTo(mouse_x, mouse_y);
        return Status::OK;
      }

      Frame* dragging_frame = Frame::GetFrameBeingDragged();
      if (dragging_frame) {
        dragging_frame->DraggedTo(mouse_x, mouse_y);
        return Status::OK;
      }

      // Test if any of the dialogs (from front to back) can handle this
      // click.
      if (Window::ForEachFrontToBackDialog([&](Window& window) {
            return window.MouseEvent(mouse_x, mouse_y, MouseButton::Unknown,
                                     false);
          }))
        return Status::OK;

      // Send the click to the frames.
      Frame* root_frame = Frame::GetRootFrame();
      if (root_frame) {
        root_frame->MouseEvent(mouse_x, mouse_y, MouseButton::Unknown, false);
      } else {
        Window::MouseNotHoveringOverWindowContents();
      }
    }

    return Status::OK;
  }

  Status MouseButton(const MouseButtonEvent& message) override {
    // Handle dropping a dragged window.
    Window* dragging_window = Window::GetWindowBeingDragged();
    if (dragging_window) {
      if (message.button != MouseButton::Left || message.is_pressed_down)
        return Status::OK;
      dragging_window->DroppedAt(mouse_x, mouse_y);
      return Status::OK;
    }

    // Handle dropping a dragged frame.
    Frame* dragging_frame = Frame::GetFrameBeingDragged();
    if (dragging_frame) {
      if (message.button != MouseButton::Left || message.is_pressed_down)
        return Status::OK;
      dragging_frame->DroppedAt(mouse_x, mouse_y);
      return Status::OK;
    }

    // Test if any of the dialogs (from front to back) can handle this
    // click.
    if (Window::ForEachFrontToBackDialog([&](Window& window) {
          return window.MouseEvent(mouse_x, mouse_y, message.button,
                                   message.is_pressed_down);
        }))
      return Status::OK;

    // Send the click to the frames.
    Frame* root_frame = Frame::GetRootFrame();
    if (root_frame) {
      root_frame->MouseEvent(mouse_x, mouse_y, message.button,
                             message.is_pressed_down);
    } else {
      Window::MouseNotHoveringOverWindowContents();
    }
    return Status::OK;
  }
};

std::unique_ptr<MyMouseListener> mouse_listener;

}  // namespace

void InitializeMouse() {
  mouse_x = GetScreenWidth() / 2;
  mouse_y = GetScreenHeight() / 2;

  mouse_listener = std::make_unique<MyMouseListener>();

  // Tell each mouse driver who we are.
  NotifyOnEachNewServiceInstance<MouseDevice>(
      [](MouseDevice::Client mouse_device) {
        // Tell the mouse driver to send us mouse messages.
        mouse_device.SetMouseListener(*mouse_listener);
      });

  // Create a texture for the mouse.
  graphics::CreateTextureRequest create_texture_request;
  create_texture_request.size.width = kMousePointerWidth;
  create_texture_request.size.height = kMousePointerHeight;

  auto create_texture_response =
      GetService<GraphicsDevice>().CreateTexture(create_texture_request);
  mouse_texture_id = create_texture_response->texture.id;
  create_texture_response->pixel_buffer->Apply([](void* data, size_t) {
    uint32* destination = (uint32*)data;
    for (int i = 0; i < kMousePointerWidth * kMousePointerHeight; i++) {
      destination[i] = kMousePointer[i];
    }
  });
}

int GetMouseX() { return mouse_x; }

int GetMouseY() { return mouse_y; }

void PrepMouseForDrawing(int min_x, int min_y, int max_x, int max_y) {
  if (min_x >= mouse_x + kMousePointerWidth ||
      min_y >= mouse_y + kMousePointerHeight || max_x <= mouse_x ||
      max_y <= mouse_y) {
    // The mouse is outside of the draw area.
    return;
  }

  CopySectionOfScreenIntoWindowManagersTexture(
      std::max(mouse_x, min_x), std::max(mouse_y, min_y),
      std::min(mouse_x + kMousePointerWidth, max_x),
      std::min(mouse_y + kMousePointerHeight, max_y));
}

void DrawMouse(graphics::Commands& commands, int min_x, int min_y, int max_x,
               int max_y) {
  if (min_x >= mouse_x + kMousePointerWidth ||
      min_y >= mouse_y + kMousePointerHeight || max_x <= mouse_x ||
      max_y <= mouse_y) {
    // The mouse is outside of the draw area.
    return;
  }

  if (commands.commands.empty()) {
    // First graphics command. Set the window manager's texture as
    // the destination texture.
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_DESTINATION_TEXTURE;
    command.texture_reference = std::make_shared<graphics::TextureReference>(
        GetWindowManagerTextureId());
  }

  // Set the mouse as the source texture.
  {
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
    command.texture_reference =
        std::make_shared<graphics::TextureReference>(mouse_texture_id);
  }

  // Draw the mouse cursor.
  {
    auto& command = commands.commands.emplace_back();
    command.type = graphics::Command::Type::SET_SOURCE_TEXTURE;
    command.position = std::make_shared<graphics::Position>(mouse_x, mouse_y);
  }
}