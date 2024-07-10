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
#include "perception/ui/builders/label.h"
#include "perception/ui/builders/node.h"
#include "perception/ui/builders/window.h"
#include "perception/ui/ui_window.h"

using ::perception::HandOverControl;
using ::perception::ui::TextAlignment;
using ::perception::ui::builders::AlignContent;
using ::perception::ui::builders::AlignText;
using ::perception::ui::builders::Height;
using ::perception::ui::builders::IsDialog;
using ::perception::ui::builders::JustifyContent;
using ::perception::ui::builders::Label;
using ::perception::ui::builders::Node;
using ::perception::ui::builders::Text;
using ::perception::ui::builders::Width;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowBackgroundColor;
using ::perception::ui::builders::WindowTitle;

int main(int argc, char *argv[]) {
  auto window = Window(WindowTitle("Welcome!"), IsDialog(),
    Label(
      JustifyContent(YGJustifyCenter),
      AlignContent(YGAlignCenter),
      Width(640),
      Height(480),
      Text("Welcome to Perception. Press the ESCAPE key to open the launcher."),
      AlignText(TextAlignment::MiddleCenter)
    ));
 
  //auto launcher = std::make_unique<Launcher>();
  HandOverControl();
  return 0;
}
