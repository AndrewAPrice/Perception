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

#include "perception/interrupts.h"
#include "perception/messages.h"
#include "perception/port_io.h"
#include "permebuf/Libraries/perception/devices/mouse_driver.permebuf.h"
#include "permebuf/Libraries/perception/devices/mouse_listener.permebuf.h"

#include <iostream>

using ::perception::ProcessId;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandler;
using ::perception::Write8BitsToPort;
using ::permebuf::perception::devices::MouseButton;
using ::permebuf::perception::devices::MouseDriver;
using ::permebuf::perception::devices::MouseListener;

namespace {

constexpr size_t kTimeout = 100000;

class PS2MouseDriver : public MouseDriver::Server {
public:
	MouseDriver() : MouseDriver_Server(),
		mouse_bytes_received_(0),
		last_button_left_(false),
		last_button_middle_(false),
		last_button_right_(false) {}

	virtual ~MouseDriver() {
		if (mouse_captor_) {
			mouse_captor->SendOnMouseRelease(MouseListener::OnMouseReleaseMessage()));
		}
	}

	void HandleMouseInterrupt() {
		uint8 val = Read8BitsFromPort(0x60);
		if (mouse_bytes_received_ == 2) {
			// Process the message now that we have all 3 bytes.
			ProcessMouseMessage(mouse_byte_buffer_[0], mouse_byte_buffer_[1], val);

			// Reset the cycle.
			mouse_bytes_received_ = 0;
		} else {
			// Read one of the first 2 bytes.
			mouse_byte_buffer[mouse_bytes_received_] = val;
			mouse_bytes_received_++;
		}
	}

	virtual void HandleSetMouseListener(ProcessId, const MouseDriver::SetMouseListenerMessage& message) {
		if (!mouse_captor_) {
			mouse_captor->SendOnMouseRelease(MouseListener::OnMouseReleaseMessage());
		}
		if (message.HasNewListener()) {
			mouse_captor_ = std::make_unique<MouseListener>(message.GetNewListener());
			mouse_captor->SendOnMouseTakenCaptiveMessage(
				MouseListener::OnMouseTakenCaptiveMessage());
		} else {
			mouse_captor_.reset();
		}
	}

private:
	// Messages from the mouse come in 3 bytes. We need to buffer these until we
	// have enough bytes to process a message.
	uint8 mouse_bytes_received_;
	uint8 mouse_byte_buffer_[2];

	// The last known state of the mouse buttons.
	bool last_button_left_;
	bool last_button_middle_;
	bool last_button_right_;

	// The service we should sent mouse events to.
	std::unique_ptr<MouseListener> mouse_captor_;

	// Processes the mouse message.
	void ProcessMouseMessage(uint8 status, uint8 offset_x, uint8 offset_y) {
		// Read if the mouse has moved.
		int16 delta_x = (int16)offset_x - (((int16)status << 4) & 0x100);
		int16 delta_y = -(int16)offset_y + (((int16)status << 3) & 0x100);

		// Reset the cycle.
		bool button_left = status & (1 << 0);
		bool button_middle = status & (1 << 2);
		bool button_right = status & (1 << 1);

		std::cout << "Mouse message: x: " << (int64)delta_x << " y: " <<
			(int64)delta_y << " left: " << button_left << " middle: " << button_middle <<
			" right: " << button_right << std::endl;

		if ((delta_x != 0 || delta_y != 0) && mouse_captor_) {
			MouseListener::OnMouseMoveMessage message;
			message.SetDeltaX(static_cast<float>(delta_x));
			message.SetDeltaY(static_cast<float>(delta_y));
			mouse_captor_->SendOnMouseMove(message);
		}

		if (button_left != last_button_left) {
			last_button_left = button_left;
			if (mouse_captor_) {
				MouseListener::OnMouseButtonMessage message;
				message.SetButton(MouseButton::Left);
				message.SetIsPressedDown(button_left);
				mouse_captor_->OnMouseButton(message);
			}
		}

		if (button_middle != last_button_middle) {
			last_button_middle = button_middle;
			if (mouse_captor_) {
				MouseListener::OnMouseButtonMessage message;
				message.SetButton(MouseButton::Middle);
				message.SetIsPressedDown(button_middle);
				mouse_captor_->OnMouseButton(message);
			}
		}

		if (button_right != last_button_right) {
			last_button_right = button_right;
			if (mouse_captor_) {
				MouseListener::OnMouseButtonMessage message;
				message.SetButton(MouseButton::Right);
				message.SetIsPressedDown(button_right);
				mouse_captor_->OnMouseButton(message);
			}
		}
	}
};

// Global instance of the mouse driver.
PS2MouseDriver mouse_driver;

void HandleKeyboardInterrupt() {
	uint8 val = Read8BitsFromPort(0x60);
	std::cout << "Keyboard: " << (size_t)val << std::endl;
}

void InterruptHandler() {
	uint8 check;

	// Keep looping while there are bytes (the mouse will send multiple bytes.)
	while ((check = Read8BitsFromPort(0x64)) & 1) {
		if (check & (1 << 5)) {
			mouse_driver.HandleMouseInterrupt();
		} else {
			HandleKeyboardInterrupt();
		}
	}
}

void WaitForMouseData() {
	size_t timeout = kTimeout;
	while (timeout--) {
		if ((Read8BitsFromPort(0x64) & 1) == 1) {
			return;
		}
	}
}

void WaitForMouseSignal() {
	size_t timeout = kTimeout;
	while (timeout--) {
		if ((Read8BitsFromPort(0x64) & 2) == 0)
			return;
	}
}

void MouseWrite(uint8 b) {
	WaitForMouseSignal();
	Write8BitsToPort(0x64, 0xD4);
	WaitForMouseSignal();
	Write8BitsToPort(0x60, b);
}

uint8 MouseRead() {
	WaitForMouseData();
	return Read8BitsFromPort(0x60);
}

void InitializePS2Controller() {
	// Enable auxiliary device.
	WaitForMouseSignal();
	Write8BitsToPort(0x64, 0xA8);
	
	// Enable the interrupts.
	WaitForMouseSignal();
	Write8BitsToPort(0x64, 0x20);
	WaitForMouseData();
	uint8 status = Read8BitsFromPort(0x60) | 2;
	WaitForMouseSignal();

	Write8BitsToPort(0x64, 0x60);
	WaitForMouseSignal();
	Write8BitsToPort(0x60, status);

	// Set the default values.
	MouseWrite(0xF6);
	(void) MouseRead();

	// Enable packet streaming.
	MouseWrite(0xF4);
	(void) MouseRead();
}

}

int main() {
	InitializePS2Controller();

	// Listen to the interrupts.
	RegisterInterruptHandler(1, InterruptHandler);
	RegisterInterruptHandler(12, InterruptHandler);

	std::cout << "PS2 controller initialized." << std::endl;

	perception::TransferToEventLoop();
	return 0;
}
