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
#include "perception/ui/ui_window.h"

using ::perception::Fiber;
using ::perception::GetProcessId;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::perception::ui::UiWindow;

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
	CreateWindow("Chicken", 0x4900a4ff, true, 400, 400);
	CreateWindow("Beef", 0x99e41dff, true, 200, 100);
	CreateWindow("Rabbit", 0x65e979ff, true, 100, 200);
	CreateWindow("Ox", 0x7c169aff, true, 80, 80);

	perception::HandOverControl();
	return 0;
}