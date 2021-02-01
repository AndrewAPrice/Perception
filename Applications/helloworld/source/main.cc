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

#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"

#include <iostream>

using ::perception::MessageId;
using ::perception::ProcessId;
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


int main() {
	MyMouseListener mouse_listener;
	std::cout << "MyMouseListener is running at PID: " << (int)mouse_listener.GetProcessId() <<
		" MID: " << (int)mouse_listener.GetMessageId() << std::endl;

	MessageId listener = MouseDriver::NotifyOnEachNewInstance(
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
				listener);
	});
	perception::TransferToEventLoop();
	return 0;
}