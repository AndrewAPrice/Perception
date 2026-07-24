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

#include "screen.h"

#include <iostream>

#include "perception/devices/graphics_device.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/services.h"
#include "perception/ui/size.h"

using ::perception::Fiber;
using ::perception::GetProcessId;
using ::perception::GetService;
using ::perception::Sleep;
using ::perception::devices::GraphicsDevice;
using ::perception::ui::Size;
namespace graphics = ::perception::devices::graphics;

namespace {

Size screen_size;
GraphicsDevice::Client graphics_device;

size_t window_manager_texture_id;
std::shared_ptr<::perception::SharedMemory> window_manager_texture_buffer;

bool screen_is_drawing;
Fiber* fiber_waiting_on_screen_to_finish_drawing;

#ifndef TEST
class GraphicsListenerServer
    : public ::perception::devices::GraphicsListener::Server {
 public:
  GraphicsListenerServer() {}
  virtual ~GraphicsListenerServer() {}

  virtual Status ScreenSizeChanged(const graphics::Size& size) override {
    screen_size = Size{.width = static_cast<float>(size.width),
                       .height = static_cast<float>(size.height)};
    return Status::OK;
  }
};

std::unique_ptr<GraphicsListenerServer> graphics_listener;
#endif

}  // namespace

void InitializeScreen() {
#ifdef TEST
  screen_size = Size{.width = 1920.0f, .height = 1080.0f};
  window_manager_texture_id = 1;
#else
  // Sleep until we get the graphics driver.
  graphics_device = GetService<GraphicsDevice>();

  // Query the screen size.
  graphics::Size graphics_screen_size;
  while (true) {
    auto status_or_size = graphics_device.GetScreenSize();
    if (status_or_size.Ok() && status_or_size->width > 0 &&
        status_or_size->height > 0) {
      graphics_screen_size = *status_or_size;
      break;
    }
    Sleep();
  }
  graphics_listener = std::make_unique<GraphicsListenerServer>();
  graphics_device.SetGraphicsListener(*graphics_listener);

  // Allow the window manager to draw to the screen.
  graphics::ProcessAllowedToDrawToScreenParameters allow_draw_to_screen_message;
  allow_draw_to_screen_message.process = GetProcessId();
  graphics_device.SetProcessAllowedToDrawToScreen(allow_draw_to_screen_message);

  // Create a texture.
  graphics::CreateTextureRequest create_texture_request;
  create_texture_request.size = graphics_screen_size;
  StatusOr<graphics::CreateTextureResponse> create_texture_response;
  while (true) {
    create_texture_response =
        graphics_device.CreateTexture(create_texture_request);
    if (create_texture_response.Ok()) {
      break;
    }
    Sleep();
  }
  window_manager_texture_id = create_texture_response->texture.id;
  window_manager_texture_buffer = create_texture_response->pixel_buffer;
  if (window_manager_texture_buffer) {
    window_manager_texture_buffer->Join();
  }

  screen_size = Size{.width = static_cast<float>(graphics_screen_size.width),
                     .height = static_cast<float>(graphics_screen_size.height)};
#endif

  fiber_waiting_on_screen_to_finish_drawing = nullptr;
  screen_is_drawing = false;
}

Size GetScreenSize() {
  return Size{.width = screen_size.width, .height = screen_size.height};
}

size_t GetWindowManagerTextureId() { return window_manager_texture_id; }

uint32* GetWindowManagerTextureData() {
#ifdef TEST
  static uint32 dummy_buffer[1920 * 1080];
  return dummy_buffer;
#else
  return reinterpret_cast<uint32*>(**window_manager_texture_buffer);
#endif
}

void SleepUntilWeAreReadyToStartDrawing() {
  if (screen_is_drawing) {
    if (fiber_waiting_on_screen_to_finish_drawing != nullptr) {
      std::cout << "Multiple fibers shouldn't be queued for the screen to "
                   "finish drawing."
                << std::endl;
    }
    fiber_waiting_on_screen_to_finish_drawing =
        perception::GetCurrentlyExecutingFiber();
    Sleep();
  }
}

#ifdef TEST
graphics::Commands last_run_draw_commands;

const graphics::Commands& GetLastRunDrawCommands() {
  return last_run_draw_commands;
}
#endif

void RunDrawCommands(
    const ::perception::devices::graphics::Commands& commands) {
  // Send the draw calls.
  screen_is_drawing = true;

#ifdef TEST
  last_run_draw_commands = commands;
  screen_is_drawing = false;
  Fiber* waiting_fiber = fiber_waiting_on_screen_to_finish_drawing;
  fiber_waiting_on_screen_to_finish_drawing = nullptr;
  if (waiting_fiber) waiting_fiber->WakeUp();
#else
  graphics_device.RunCommands(commands, [](Status response) {
    screen_is_drawing = false;
    Fiber* waiting_fiber = fiber_waiting_on_screen_to_finish_drawing;
    fiber_waiting_on_screen_to_finish_drawing = nullptr;
    if (waiting_fiber) waiting_fiber->WakeUp();
  });
#endif
}
