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
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "window.h"
#include "screen.h"

using ::perception::MessageId;
using ::perception::ProcessId;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;
namespace {

int mouse_x;
int mouse_y;
int mouse_texture_id;

constexpr uint32 kMousePointer[] = {
		0x000000FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x000000FF, 0x000000FF,
		0x000000FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000, 0x00000000,
		0x000000FF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0xC3C3C3FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000,
		0x000000FF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x000000FF, 0xC3C3C3FF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000, 0x00000000,
		0x000000FF, 0x000000FF, 0x00000000, 0x00000000, 0x000000FF, 0xC3C3C3FF, 0xFFFFFFFF, 0xFFFFFFFF, 0xC3C3C3FF, 0x000000FF, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000000FF, 0xC3C3C3FF, 0xC3C3C3FF, 0xC3C3C3FF, 0x000000FF, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x000000FF, 0x000000FF, 0x000000FF, 0x00000000, 0x00000000
	};

constexpr int kMousePointerWidth = 11;
constexpr int kMousePointerHeight = 17;

class MyMouseListener : public MouseListener::Server {
public:
	void HandleOnMouseMove(
		ProcessId, const MouseListener::OnMouseMoveMessage& message) override {
		int old_mouse_x = mouse_x;
		int old_mouse_y = mouse_y;

		mouse_x += static_cast<int>(message.GetDeltaX());
		mouse_y += static_cast<int>(message.GetDeltaY());

		mouse_x = std::max(0, std::min(mouse_x, GetScreenWidth() - 1));
		mouse_y = std::max(0, std::min(mouse_y, GetScreenHeight() - 1));

		// Has the mouse moved?
		if (mouse_x != old_mouse_x || mouse_y != old_mouse_y) {
			// Invalidate the area under the cursor.
			InvalidateScreen(
				std::min(mouse_x, old_mouse_x),
				std::min(mouse_y, old_mouse_y),
				std::max(mouse_x, old_mouse_x) + 11,
				std::max(mouse_y, old_mouse_y) + 17);

			Window *dragging_window = Window::GetWindowBeingDragged();
			if (dragging_window) {
				dragging_window->DraggedTo(mouse_x, mouse_y);
				return;
			}

			Frame* dragging_frame = Frame::GetFrameBeingDragged();
			if (dragging_frame) {
				dragging_frame->DraggedTo(mouse_x, mouse_y);
				return;
			}

			// Test if any of the dialogs (from front to back) can handle this
			// click.
			if (Window::ForEachFrontToBackDialog([&](Window& window) {
					return window.MouseEvent(mouse_x, mouse_y,
						MouseButton::Unknown, false);
				}))
				return;

			// Send the click to the frames.
			Frame* root_frame = Frame::GetRootFrame();
			if (root_frame) {
				root_frame->MouseEvent(mouse_x, mouse_y,
					MouseButton::Unknown, false);
			} else {
				Window::MouseNotHoveringOverWindowContents();
			}
		}
	}

	void HandleOnMouseButton(
		ProcessId, const MouseListener::OnMouseButtonMessage& message) override {
		// Handle dropping a dragged window.
		Window *dragging_window = Window::GetWindowBeingDragged();
		if (dragging_window) {
			if (message.GetButton() != MouseButton::Left ||
				message.GetIsPressedDown())
				return;
			dragging_window->DroppedAt(mouse_x, mouse_y);
			return;
		}

		// Handle dropping a dragged frame.
		Frame *dragging_frame = Frame::GetFrameBeingDragged();
		if (dragging_frame) {
			if (message.GetButton() != MouseButton::Left ||
				message.GetIsPressedDown())
				return;
			dragging_frame->DroppedAt(mouse_x, mouse_y);
			return;
		}

		// Test if any of the dialogs (from front to back) can handle this
		// click.
		if (Window::ForEachFrontToBackDialog([&](Window& window) {
				return window.MouseEvent(mouse_x, mouse_y,
					message.GetButton(), message.GetIsPressedDown());
			}))
			return;

		// Send the click to the frames.
		Frame* root_frame = Frame::GetRootFrame();
		if (root_frame) {
			root_frame->MouseEvent(mouse_x, mouse_y,
				message.GetButton(), message.GetIsPressedDown());
		} else {
			Window::MouseNotHoveringOverWindowContents();
		}
	}
};

std::unique_ptr<MyMouseListener> mouse_listener;

}

void InitializeMouse() {
	mouse_x = GetScreenWidth() / 2;
	mouse_y = GetScreenHeight() / 2;

	mouse_listener = std::make_unique<MyMouseListener>();

	// Tell each mouse driver who we are.
	(void) MouseDriver::NotifyOnEachNewInstance(
		[] (MouseDriver mouse_driver) {
			// Tell the mouse driver to send us mouse messages.
			MouseDriver::SetMouseListenerMessage message;
			message.SetNewListener(*mouse_listener);
			mouse_driver.SendSetMouseListener(message); 
		});

	// Create a texture for the mouse.
	GraphicsDriver::CreateTextureRequest create_texture_request;
	create_texture_request.SetWidth(kMousePointerWidth);
	create_texture_request.SetHeight(kMousePointerHeight);

	auto create_texture_response = *GetGraphicsDriver().CallCreateTexture(
		create_texture_request);
	mouse_texture_id = create_texture_response.GetTexture();
	create_texture_response.GetPixelBuffer().Apply([](void* data, size_t) {
		uint32* destination = (uint32*)data;
		for (int i = 0; i < kMousePointerWidth * kMousePointerHeight; i++) {
			destination[i] = kMousePointer[i];
		}
	});
}

int GetMouseX() {
	return mouse_x;
}

int GetMouseY() {
	return mouse_y;
}


void PrepMouseForDrawing(int min_x, int min_y, int max_x, int max_y) {
	if (min_x >= mouse_x + kMousePointerWidth ||
		min_y >= mouse_y + kMousePointerHeight ||
		max_x <= mouse_x || max_y <= mouse_y) {
		// The mouse is outside of the draw area.
		return;
	}

	CopySectionOfScreenIntoWindowManagersTexture(
		std::max(mouse_x, min_x), std::max(mouse_y, min_y),
		std::min(mouse_x + kMousePointerWidth, max_x),
		std::min(mouse_y + kMousePointerHeight, max_y));
}

void DrawMouse(
	Permebuf<GraphicsDriver::RunCommandsMessage>& commands,
	PermebufListOfOneOfs<GraphicsCommand>& last_graphics_command,
	int min_x, int min_y, int max_x, int max_y) {
	if (min_x >= mouse_x + kMousePointerWidth ||
		min_y >= mouse_y + kMousePointerHeight ||
		max_x <= mouse_x || max_y <= mouse_y) {
		// The mouse is outside of the draw area.
		return;
	}

	if (!last_graphics_command.IsValid()) {
		// First graphics command. Set the window manager's texture as
		// the destination texture.
		last_graphics_command = commands->MutableCommands();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_graphics_command.Set(command_one_of);
		command_one_of.MutableSetDestinationTexture()
			.SetTexture(GetWindowManagerTextureId());
	}

	// Set the mouse as the source texture.
	last_graphics_command = last_graphics_command.InsertAfter();
	auto texture_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_graphics_command.Set(texture_oneof);
	texture_oneof.MutableSetSourceTexture()
		.SetTexture(mouse_texture_id);

	// Draw the mouse cursor.
	last_graphics_command = last_graphics_command.InsertAfter();
	auto draw_command_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_graphics_command.Set(draw_command_oneof);
	auto copy_texture_with_alpha =
		draw_command_oneof.MutableCopyTextureToPositionWithAlphaBlending();
	copy_texture_with_alpha.SetLeftDestination(mouse_x);
	copy_texture_with_alpha.SetTopDestination(mouse_y);
}