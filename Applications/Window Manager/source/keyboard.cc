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

#include "keyboard.h"

#include "perception/fibers.h"
#include "perception/messages.h"

using ::perception::GetCurrentlyExecutingFiber;
using ::perception::MessageId;
using ::perception::Sleep;
using ::permebuf::perception::devices::KeyboardDriver;

namespace {

KeyboardDriver keyboard_driver;

}

void InitializeKeyboard() {
	// Sleep until we get the keyboard driver.
	auto main_fiber = GetCurrentlyExecutingFiber();
	MessageId keyboard_driver_listener = KeyboardDriver::NotifyOnEachNewInstance(
		[main_fiber] (KeyboardDriver driver) {
			keyboard_driver = driver;
			main_fiber->WakeUp();
		});
	Sleep();

	// We only care about one instance. We can stop
	// listening now.
	KeyboardDriver::StopNotifyingOnEachNewInstance(
		keyboard_driver_listener);
}

const KeyboardDriver& GetKeyboardDriver() {
	std::cout << "keyboard_driver is " << keyboard_driver.GetProcessId() << ":" << keyboard_driver.GetMessageId() << std::endl;
	return keyboard_driver;
}