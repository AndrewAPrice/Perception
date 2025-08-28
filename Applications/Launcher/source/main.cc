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
#include <sstream>
#include <vector>

#include "launcher.h"
#include "perception/scheduler.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::ui::Layout;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

int main(int argc, char* argv[]) {
  auto window = UiWindow::DialogWithTitleBar(
      "Welcome!",
      Label::BasicLabel(
          "Welcome to Perception. Press the ESCAPE key to open the launcher.",
          [](Layout& layout) {
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignContent(YGAlignCenter);
          },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleCenter);
          }));

  // auto launcher = std::make_unique<Launcher>();
  HandOverControl();
  return 0;
}
