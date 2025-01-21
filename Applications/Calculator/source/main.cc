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

#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/ui_window.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::UiWindow;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;

namespace {

constexpr uint32 kButtonPanelBackgroundColor =
    SkColorSetARGB(0xFF, 0xC7, 0xC7, 0xC7);
constexpr uint32 kTerminalBackgroundColor =
    SkColorSetARGB(0xFF, 0xF7, 0xF7, 0xF7);

enum Operation { NOTHING = 0, ADD = 1, SUBTRACT = 2, DIVIDE = 3, MULTIPLY = 4 };
Operation operation = NOTHING;
double last_number = 0.0;
double current_number = 0.0;
bool any_number = false;
bool decimal_pressed = false;
double decimal_multiplier = 0.1;

std::shared_ptr<perception::ui::components::Label> display;
std::shared_ptr<perception::ui::Node> terminal_content;

void UpdateDisplay() {
  // std::to_string converts the number to fixed decimal places, which doesn't
  // behave as you'd expect a calculator to.
  std::stringstream buffer;
  buffer << current_number;
  if (decimal_pressed && decimal_multiplier == 0.1) {
    display->SetText(buffer.str() + ".");
  } else {
    display->SetText(buffer.str());
  }
}

void PressNumber(int number) {
  if (!any_number) {
    current_number = static_cast<double>(number);
  } else if (decimal_pressed) {
    current_number += static_cast<double>(number) * decimal_multiplier;
    decimal_multiplier /= 10.0;
  } else {
    current_number *= 10;
    current_number += static_cast<double>(number);
  }
  any_number = true;
  UpdateDisplay();
}

void PressFlipSign() {
  current_number *= -1;
  UpdateDisplay();
}

void PressDecimal() {
  if (decimal_pressed) return;

  decimal_pressed = true;
  decimal_multiplier = 0.1;

  if (!any_number) {
    current_number = 0.0;
  }
  UpdateDisplay();
}

void PressEquals() {
  switch (operation) {
    default:
      return;
    case ADD:
      current_number = last_number + current_number;
      break;
    case SUBTRACT:
      current_number = last_number - current_number;
      break;
    case DIVIDE:
      if (current_number != 0) current_number = last_number / current_number;
      break;
    case MULTIPLY:
      current_number = last_number * current_number;
      break;
  }
  operation = NOTHING;
  UpdateDisplay();
  any_number = false;
  decimal_pressed = false;
}

void PressOperator(Operation new_operator) {
  if (operation != NOTHING) PressEquals();

  operation = new_operator;
  last_number = current_number;
  any_number = false;
  decimal_pressed = false;
  current_number = 0.0;
  UpdateDisplay();
}

void PressClear() {
  current_number = 0.0;
  decimal_pressed = false;
  UpdateDisplay();
}

}  // namespace

int main(int argc, char* argv[]) {
  auto display_node = Label::BasicLabel(
      "",
      [](Label& label) { label.SetTextAlignment(TextAlignment::MiddleCenter); },
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetFlexGrow(1.0);
      });
  display = display_node->Get<perception::ui::components::Label>();

  //  terminal_content = Node(FlexDirection(YGFlexDirectionColumn));
  /*for (int i = 0; i < 80; i++) {
    terminal_content->AddChild(
        std::make_shared<Label>()->SetLabel("Hello")->ToSharedPtr());
  }*/

  /*auto terminal_container =
     ScrollContainer(terminal_content, ShowVerticalScrollBar(),
                     FillColor(kTerminalBackgroundColor), BorderWidth(0.0f),
                     BorderRadius(0.0f), FlexGrow(1.0),
                     AlignSelf(YGAlignStretch), Margin(YGEdgeAll, 0.0f));*/

  auto terminal_container = Block::SolidColor(
      kTerminalBackgroundColor,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetMargin(YGEdgeAll, 0.f);
      },
      display_node);
  /*
    window->OnResize([](float width, float height) {
      bool is_landscape = width > height;
      if (auto top_level_widget =
              Widget::GetWidgetWithId(kTopLevelWidgetId).lock()) {
        top_level_widget->SetFlexDirection(
            is_landscape ? YGFlexDirectionRowReverse : YGFlexDirectionColumn);
      }
    });*/

  auto button_panel = Block::SolidColor(
      kButtonPanelBackgroundColor,
      [](Layout& layout) {
        layout.SetWidth(194.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetAlignContent(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetAlignSelf(YGAlignCenter);
            layout.SetWidthAuto();
          },
          Container::HorizontalContainer(
              Button::TextButton("C", PressClear),
              Button::TextButton("+-", PressFlipSign),
              Button::TextButton("/", std::bind(PressOperator, DIVIDE)),
              Button::TextButton("x", std::bind(PressOperator, MULTIPLY))),
          Container::HorizontalContainer(
              Button::TextButton("7", std::bind(PressNumber, 7)),
              Button::TextButton("8", std::bind(PressNumber, 8)),
              Button::TextButton("9", std::bind(PressNumber, 9)),
              Button::TextButton("-", std::bind(PressOperator, SUBTRACT))),
          Container::HorizontalContainer(
              Button::TextButton("4", std::bind(PressNumber, 4)),
              Button::TextButton("5", std::bind(PressNumber, 5)),
              Button::TextButton("6", std::bind(PressNumber, 6)),
              Button::TextButton("+", std::bind(PressOperator, ADD))),
          Container::HorizontalContainer(
              Container::VerticalContainer(
                  Container::HorizontalContainer(
                      Button::TextButton("1", std::bind(PressNumber, 1)),
                      Button::TextButton("2", std::bind(PressNumber, 2)),
                      Button::TextButton("3", std::bind(PressNumber, 3))),
                  Container::HorizontalContainer(
                      Button::TextButton(
                          "0", std::bind(PressNumber, 0),
                          [](Layout& layout) { layout.SetWidth(58); }),
                      Button::TextButton(".", PressDecimal))),
              Button::TextButton("=", std::bind(PressEquals)))));

  auto window = UiWindow::BasicWindow(
      "Calculator",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetFlexGrow(1.0); }, button_panel,
          terminal_container));

  HandOverControl();
  return 0;
}
