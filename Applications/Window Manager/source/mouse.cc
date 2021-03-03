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

#include <iostream>

using ::perception::MessageId;
using ::perception::ProcessId;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;

namespace {

int mouse_x;
int mouse_y;
MessageId mouse_driver_listener;

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

			// TODO: Handle enter/leaving/hovering.
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
				return window.MouseButtonEvent(mouse_x, mouse_y,
					message.GetButton(), message.GetIsPressedDown());
			}))
			return;

		// Send the click to the frames.
		Frame* root_frame = Frame::GetRootFrame();
		if (root_frame)
			root_frame->MouseButtonEvent(mouse_x, mouse_y,
				message.GetButton(), message.GetIsPressedDown());
	}
};

std::unique_ptr<MyMouseListener> mouse_listener;

}

void InitializeMouse() {
	mouse_x = GetScreenWidth() / 2;
	mouse_y = GetScreenHeight() / 2;

	mouse_listener = std::make_unique<MyMouseListener>();

	// Sleep until we get the mouse driver.
	mouse_driver_listener = MouseDriver::NotifyOnEachNewInstance(
		[&] (MouseDriver mouse_driver) {
			// Tell the mouse driver to send us mouse messages.
			MouseDriver::SetMouseListenerMessage message;
			message.SetNewListener(*mouse_listener);
			mouse_driver.SendSetMouseListener(message); 

			// We only care about one instance. We can stop
			// listening now.
			MouseDriver::StopNotifyingOnEachNewInstance(
				mouse_driver_listener);
		});
}

int GetMouseX() {
	return mouse_x;
}

int GetMouseY() {
	return mouse_y;
}