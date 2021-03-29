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
#include "perception/time.h"
#include "perception/ui/ui_window.h"

using ::perception::Fiber;
using ::perception::GetProcessId;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::perception::ui::UiWindow;

using ::perception::AfterDuration;
using ::perception::SleepUntilTimeSinceKernelStarted;
using ::perception::SleepForDuration;
using ::perception::AfterTimeSinceKernelStarted;
using ::perception::GetTimeSinceKernelStarted;

void CreateWindow(std::string_view title, uint32 color,
	bool dialog = false, int width = 0, int height = 0) {
	new UiWindow(title, dialog, width, height);
}

int main() {
	CreateWindow("Raspberry", 0x0ed321ff);
	CreateWindow("Blueberry", 0xc5c20dff);
	CreateWindow("Blackberry", 0xa5214eff);
	CreateWindow("Strawberry", 0x90bdee);
	CreateWindow("Boysenberry", 0x25993fff);
	CreateWindow("Popup Dialog", 0x65e979ff, true, 100, 200);
	CreateWindow("Another Dialog", 0x7c169aff, true, 80, 80);

	std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count() << std::endl;
	std::cout << "Before" << std::endl;
	SleepForDuration(std::chrono::seconds(1));
	std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count() << std::endl;
	SleepForDuration(std::chrono::seconds(1));
	std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count() << std::endl;

	AfterDuration(std::chrono::seconds(3), [&]() {
		std::cout << "3 (" << GetTimeSinceKernelStarted().count() << ")" << std::endl;
	});
	AfterDuration(std::chrono::seconds(1), [&]() {
		std::cout << "1 (" << GetTimeSinceKernelStarted().count() << ")" << std::endl;
	});
	AfterDuration(std::chrono::seconds(4), [&]() {
		std::cout << "4 (" << GetTimeSinceKernelStarted().count() << ")" << std::endl;
	});
	AfterDuration(std::chrono::seconds(2), [&]() {
		std::cout << "2 (" << GetTimeSinceKernelStarted().count() << ")" << std::endl;
	});

	AfterTimeSinceKernelStarted(GetTimeSinceKernelStarted() +
		std::chrono::milliseconds(2500), []() {
		std::cout << "2.5 (" << GetTimeSinceKernelStarted().count() << ")" << std::endl;
		});


	perception::HandOverControl();
	return 0;
}