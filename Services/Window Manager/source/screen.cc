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
#include "perception/shared_memory.h"

namespace graphics = ::perception::devices::graphics;
using ::perception::Fiber;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::GetProcessId;
using ::perception::GetService;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::SharedMemory;
using ::perception::Sleep;
using ::perception::Status;
using ::perception::devices::GraphicsDevice;

namespace {

GraphicsDevice::Client graphics_device;
graphics::Size screen_size;
int window_manager_texture_id;
std::shared_ptr<SharedMemory> window_manager_texture_buffer;

bool screen_is_drawing;
Fiber* fiber_waiting_on_screen_to_finish_drawing;

}  // namespace

void InitializeScreen() {
  // Sleep until we get the graphics driver.
  graphics_device = GetService<GraphicsDevice>();
  // Query the screen size.
  screen_size = *graphics_device.GetScreenSize();

  // Allow the window manager to draw to the screen.
  graphics::ProcessAllowedToDrawToScreenParameters allow_draw_to_screen_message;
  allow_draw_to_screen_message.process = GetProcessId();
  graphics_device.SetProcessAllowedToDrawToScreen(allow_draw_to_screen_message);

  // Create a texture.
  graphics::CreateTextureRequest create_texture_request;
  create_texture_request.size = screen_size;
  auto create_texture_response =
      graphics_device.CreateTexture(create_texture_request);
  window_manager_texture_id = create_texture_response->texture.id;
  window_manager_texture_buffer = create_texture_response->pixel_buffer;
  window_manager_texture_buffer->Join();

  fiber_waiting_on_screen_to_finish_drawing = nullptr;
  screen_is_drawing = false;
}

int GetScreenWidth() { return screen_size.width; }

int GetScreenHeight() { return screen_size.height; }

size_t GetWindowManagerTextureId() { return window_manager_texture_id; }

uint32* GetWindowManagerTextureData() {
  return reinterpret_cast<uint32*>(**window_manager_texture_buffer);
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

void RunDrawCommands(
    const ::perception::devices::graphics::Commands& commands) {
  // Send the draw calls.
  screen_is_drawing = true;

  graphics_device.RunCommands(commands, [](Status response) {
    screen_is_drawing = false;
    Fiber* waiting_fiber = fiber_waiting_on_screen_to_finish_drawing;
    fiber_waiting_on_screen_to_finish_drawing = nullptr;
    if (waiting_fiber) waiting_fiber->WakeUp();
  });
}
