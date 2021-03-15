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

#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/shared_memory.h"

#include <iostream>

using ::perception::GetCurrentlyExecutingFiber;
using ::perception::GetProcessId;
using ::perception::Fiber;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::SharedMemory;
using ::perception::Sleep;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;

namespace {

GraphicsDriver graphics_driver;
int screen_width;
int screen_height;
int window_manager_texture_id;
SharedMemory window_manager_texture_buffer;

bool screen_is_drawing;
bool waiting_on_screen_to_finish_drawing;

}

void InitializeScreen() {
	// Sleep until we get the graphics driver.
	graphics_driver = GraphicsDriver::Get();
	
	// Query the screen size.
	auto screen_size = *graphics_driver.CallGetScreenSize(
		GraphicsDriver::GetScreenSizeRequest());
	screen_width = screen_size.GetWidth();
	screen_height = screen_size.GetHeight();

	// Allow the window manager to draw to the screen.
	GraphicsDriver::SetProcessAllowedToDrawToScreenMessage allow_draw_to_screen_message;
	allow_draw_to_screen_message.SetProcess(GetProcessId());
	graphics_driver.SendSetProcessAllowedToDrawToScreen(allow_draw_to_screen_message);

	// Create a texture.
	GraphicsDriver::CreateTextureRequest create_texture_request;
	create_texture_request.SetWidth(screen_width);
	create_texture_request.SetHeight(screen_height);

	auto create_texture_response = *graphics_driver.CallCreateTexture(
		create_texture_request);
	window_manager_texture_id = create_texture_response.GetTexture();
	window_manager_texture_buffer = std::move(create_texture_response.GetPixelBuffer());
	window_manager_texture_buffer.Join();

	screen_is_drawing = false;
	waiting_on_screen_to_finish_drawing = false;
}

::permebuf::perception::devices::GraphicsDriver& GetGraphicsDriver() {
	return graphics_driver;
}

int GetScreenWidth() {
	return screen_width;
}

int GetScreenHeight() {
	return screen_height;
}

size_t GetWindowManagerTextureId() {
	return window_manager_texture_id;
}

uint32* GetWindowManagerTextureData() {
	return reinterpret_cast<uint32*>(*window_manager_texture_buffer);
}

void SleepUntilWeAreReadyToStartDrawing() {
	if (screen_is_drawing) {
		waiting_on_screen_to_finish_drawing = true;
		Sleep();
	}
}

void RunDrawCommands(Permebuf<
	::permebuf::perception::devices::GraphicsDriver::RunCommandsMessage> commands) {
	// Send the draw calls.
	screen_is_drawing = true;

	auto main_fiber = perception::GetCurrentlyExecutingFiber();
	graphics_driver.CallRunCommandsAndWait(std::move(commands),
		[main_fiber](StatusOr<GraphicsDriver::EmptyResponse> response) { 
			screen_is_drawing = false;
			if (waiting_on_screen_to_finish_drawing) {
				waiting_on_screen_to_finish_drawing = false;
				main_fiber->WakeUp();
			}
		});

}

