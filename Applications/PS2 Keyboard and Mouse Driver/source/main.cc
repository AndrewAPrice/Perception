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

#include <iostream>

using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandler;
using ::perception::Write8BitsToPort;

namespace {

constexpr size_t kTimeout = 100000;

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

// The last known state of the mouse buttons.
bool last_button_left = false;
bool last_button_middle = false;
bool last_button_right = false;

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
}

// Messages from the mouse come in 3 bytes. We need to buffer these until we
// have enough bytes to process a message.
uint8 mouse_bytes_received = 0;
uint8 mouse_byte_buffer[2];

void HandleMouseInterrupt() {
	uint8 val = Read8BitsFromPort(0x60);
	if (mouse_bytes_received == 2) {
		// Process the message now that we have all 3 bytes.
		ProcessMouseMessage(mouse_byte_buffer[0], mouse_byte_buffer[1], val);

		// Reset the cycle.
		mouse_bytes_received = 0;
	} else {
		// Read one of the first 2 bytes.
		mouse_byte_buffer[mouse_bytes_received] = val;
		mouse_bytes_received++;
	}
}

void HandleKeyboardInterrupt() {
	uint8 val = Read8BitsFromPort(0x60);
	std::cout << "Keyboard: " << (size_t)val << std::endl;
}

// TODO: Fire event on interrupt.
void InterruptHandler() {
	uint8 check;

	// Keep looping while there are mouse bytes.
	while ((check = Read8BitsFromPort(0x64)) & 1) {
		if (check & (1 << 5)) {
			HandleMouseInterrupt();
		} else {
			HandleKeyboardInterrupt();
		}
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
