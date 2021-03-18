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

#include <iostream>
#include <vector>
#include <sstream>

#include "launcher.h"
#include "perception/scheduler.h"
#include "perception/ui/label.h"
#include "perception/ui/ui_window.h"

using ::perception::HandOverControl;
using ::perception::ui::kFillParent;
using ::perception::ui::Label;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;

int main() {
	auto window = std::make_shared<UiWindow>("Welcome!", true, 300, 50);
	window->SetRoot(
		std::make_shared<Label>()->
		SetTextAlignment(TextAlignment::MiddleCenter)->
		SetLabel("Welcome to Perception. Press the ESCAPE key to open the launcher.")->
		SetSize(kFillParent)->ToSharedPtr());

	auto launcher = std::make_unique<Launcher>();
	HandOverControl();
	return 0;
}
