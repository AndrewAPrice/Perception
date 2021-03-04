// Copyright 2020 Google LLC
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

#include <iostream>
#include <vector>

#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_listener.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"
#include "permebuf/Libraries/perception/window.permebuf.h"
#include "permebuf/Libraries/perception/window_manager.permebuf.h"

using ::perception::Fiber;
using ::perception::GetProcessId;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;
using ::permebuf::perception::Window;
using ::permebuf::perception::WindowManager;

WindowManager window_manager;

class MyWindow : public MouseListener::Server,
				 public KeyboardListener::Server,
				 public Window::Server {
public:
	MyWindow(std::string_view title, uint32 bg_color,
			bool dialog = false, int dialog_width = 0, int dialog_height = 0) :
		title_(title) {
		Permebuf<WindowManager::CreateWindowRequest> create_window_request;
		create_window_request->SetWindow(*this);
		create_window_request->SetTitle(title);
		create_window_request->SetFillColor(bg_color);
		create_window_request->SetKeyboardListener(*this);
		create_window_request->SetMouseListener(*this);
		if (dialog) {
			create_window_request->SetIsDialog(true);
			create_window_request->SetDesiredDialogWidth(dialog_width);
			create_window_request->SetDesiredDialogHeight(dialog_height);
		}

		auto status_or_result = window_manager.CallCreateWindow(
			std::move(create_window_request));
		if (status_or_result) {
			std::cout << title_ << " is " << status_or_result->GetWidth() << "x" <<
				status_or_result->GetHeight() << std::endl;
		}
	}

	void HandleOnMouseMove(
		ProcessId, const MouseListener::OnMouseMoveMessage& message) override {
			std::cout << title_ << " - x:" << message.GetDeltaX() <<
				" y:" << message.GetDeltaY() << std::endl;
	}

	void HandleOnMouseScroll(
		ProcessId, const MouseListener::OnMouseScrollMessage& message) override {
		std::cout << title_ << " mouse scroled" << message.GetDelta() << std::endl;
	}

	void HandleOnMouseButton(
		ProcessId, const MouseListener::OnMouseButtonMessage& message) override {
		std::cout << title_ << " - button: " << (int)message.GetButton() <<
			" down: " << message.GetIsPressedDown() << std::endl;
	}

	void HandleOnMouseClick(
		ProcessId, const MouseListener::OnMouseClickMessage& message) override {
		std::cout << title_ << " - mouse clicked - button: " << (int)message.GetButton() <<
			" down: " << message.GetWasPressedDown() << " x: " << message.GetX() <<
			" y: " << message.GetY() << std::endl;
	}

	void HandleOnMouseHover(
		ProcessId, const MouseListener::OnMouseHoverMessage& message) override {
		std::cout << title_ << " - mouse hover: " << " x: " << message.GetX() <<
			" y: " << message.GetY() << std::endl;
	}

	void HandleOnMouseTakenCaptive(
		ProcessId, const MouseListener::OnMouseTakenCaptiveMessage& message) override {
		std::cout << title_ << " - mouse taken captive." << std::endl;
	}

	void HandleOnMouseReleased(
		ProcessId, const MouseListener::OnMouseReleasedMessage& message) override {
		std::cout << title_ << " - mouse has been released." << std::endl;
	}

	void HandleOnKeyDown(ProcessId,
		const KeyboardListener::OnKeyDownMessage& message) override {
		std::cout << title_ << " - key " << (int)message.GetKey() << " was pressed." << std::endl;
	}

	void HandleOnKeyUp(ProcessId,
		const KeyboardListener::OnKeyUpMessage& message) override {
		std::cout << title_ << " - key " << (int)message.GetKey() << " was released." << std::endl;
	}

	void HandleOnKeyboardTakenCaptive(ProcessId,
		const KeyboardListener::OnKeyboardTakenCaptiveMessage& message) override {
		std::cout << title_ << " - keyboard taken captive." << std::endl;
	}

	void HandleOnKeyboardReleased(ProcessId,
		const KeyboardListener::OnKeyboardReleasedMessage& message) override {
		std::cout << title_ << " - keyboard has been released." << std::endl;
	}

	void HandleSetSize(ProcessId,
		const Window::SetSizeMessage& message) override {
		std::cout << title_ << " is " << message.GetWidth() << "x" <<
				message.GetHeight() << std::endl;
	}

	void HandleClosed(ProcessId,
		const Window::ClosedMessage& message) override {
		std::cout << title_ << " closed" << std::endl;
	}

	void HandleGainedFocus(ProcessId,
		const Window::GainedFocusMessage& message) override {
		std::cout << title_ << " gained focus" << std::endl;
	}

	void HandleLostFocus(ProcessId,
		const Window::LostFocusMessage& message) override {
		std::cout << title_ << " lost focus" << std::endl;
	}
private:
	std::string title_;
};

int main() {
	// Sleep until we get the graphics driver.
	auto main_fiber = perception::GetCurrentlyExecutingFiber();
	MessageId wm_listener = WindowManager::NotifyOnEachNewInstance(
		[&] (WindowManager wm) {
			window_manager = wm;
			// We only care about one instance. We can stop
			// listening now.
			WindowManager::StopNotifyingOnEachNewInstance(
				wm_listener);

			main_fiber->WakeUp();
		});
	Sleep();

	std::vector<MyWindow> windows = {
		MyWindow("Raspberry", 0x0ed321ff),
		MyWindow("Blueberry", 0xc5c20dff),
		MyWindow("Blackberry", 0xa5214eff),
		MyWindow("Strawberry", 0x90bdee),
		MyWindow("Boysenberry", 0x25993fff),
		MyWindow("Chicken", 0x4900a4ff, true, 400, 400),
		MyWindow("Beef", 0x99e41dff, true, 200, 100),
		MyWindow("Rabbit", 0x65e979ff, true, 100, 200),
		MyWindow("Ox", 0x7c169aff, true, 80, 80)
	};

	perception::HandOverControl();
	return 0;
}