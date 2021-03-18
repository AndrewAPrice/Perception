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

#include "launcher_window.h"

#include <iostream>
#include <memory>

#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/ui/label.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/ui_window.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"

using ::perception::Defer;
using ::perception::TerminateProcess;
using ::perception::ui::kFillParent;
using ::perception::ui::Label;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;
using ::permebuf::perception::devices::GraphicsDriver;

namespace {

std::shared_ptr<UiWindow> launcher_window;

}

void ShowLauncherWindow() {
	if (launcher_window) {
		// Launcher window is already open.
		return;
	}

	// Query the screen size.
	auto screen_size = *GraphicsDriver::Get().CallGetScreenSize(
		GraphicsDriver::GetScreenSizeRequest());

	// Create the launcher that's 80% of the screen size.
	int launcher_width = screen_size.GetWidth() * 8 / 10;
	int launcher_height = screen_size.GetHeight() * 8 / 10;

	launcher_window = std::make_shared<UiWindow>("Launcher",
		true, launcher_width, launcher_height);
	launcher_window->SetRoot(
		std::make_shared<Label>()->
		SetTextAlignment(TextAlignment::MiddleCenter)->
		SetLabel("TODO: Implement")->
		SetSize(kFillParent)->ToSharedPtr())->OnClose([]() {
			Defer([]() {
				launcher_window.reset();
			});
		});
}
