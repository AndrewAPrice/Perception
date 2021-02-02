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

#include "permebuf/Libraries/perception/devices/keyboard_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/keyboard_listener.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"

#include <iostream>

using ::perception::MessageId;
using ::perception::ProcessId;
using ::permebuf::perception::devices::KeyboardDriver;
using ::permebuf::perception::devices::KeyboardListener;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;

class MyMouseListener : public MouseListener::Server {
public:
	void HandleOnMouseMove(
		ProcessId, const MouseListener::OnMouseMoveMessage& message) override {
			std::cout << "X:" << message.GetDeltaX() <<
				" Y:" << message.GetDeltaY() << std::endl;
	}

	void HandleOnMouseButton(
		ProcessId, const MouseListener::OnMouseButtonMessage& message) override {
		std::cout << "Button: " << (int)message.GetButton() <<
			" Down: " << message.GetIsPressedDown() << std::endl;
	}
};

class MyKeyboardListener : public KeyboardListener::Server {
public:
	void HandleOnKeyDown(ProcessId,
		const KeyboardListener::OnKeyDownMessage& message) override {
		std::cout << "Key " << (int)message.GetKey() << " was pressed." << std::endl;
	}

	void HandleOnKeyUp(ProcessId,
		const KeyboardListener::OnKeyUpMessage& message) override {
		std::cout << "Key " << (int)message.GetKey() << " was released." << std::endl;
	}
};

int main() {
	MyMouseListener mouse_listener;
	MyKeyboardListener keyboard_listener;

	MessageId mouse_driver_listener = MouseDriver::NotifyOnEachNewInstance(
		[&] (MouseDriver mouse_driver) {
			std::cout << "I'm notified that there's a mouse driver at PID: " <<
				(int)mouse_driver.GetProcessId() << " MID: " << (int)mouse_driver.GetMessageId() <<
					std::endl;
			// Tell the mouse driver to send us mouse messages.
			MouseDriver::SetMouseListenerMessage message;
			message.SetNewListener(mouse_listener);
			mouse_driver.SendSetMouseListener(message); 

			// We only care about one instance. We can stop
			// listening now.
			MouseDriver::StopNotifyingOnEachNewInstance(
				mouse_driver_listener);
	});

	MessageId keyboard_driver_listener = KeyboardDriver::NotifyOnEachNewInstance(
		[&] (KeyboardDriver keyboard_driver) {
			std::cout << "I'm notified that there's a keyboard driver at PID: " <<
				(int)keyboard_driver.GetProcessId() << " MID: " <<
				(int)keyboard_driver.GetMessageId() <<
					std::endl;
			// Tell the mouse driver to send us mouse messages.
			KeyboardDriver::SetKeyboardListenerMessage message;
			message.SetNewListener(keyboard_listener);
			keyboard_driver.SendSetKeyboardListener(message); 

			// We only care about one instance. We can stop
			// listening now.
			KeyboardDriver::StopNotifyingOnEachNewInstance(
				keyboard_driver_listener);
	});
	perception::TransferToEventLoop();
	return 0;
}