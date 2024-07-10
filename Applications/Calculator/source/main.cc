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
#include "perception/ui/builders/block.h"
#include "perception/ui/builders/button.h"
#include "perception/ui/builders/label.h"
#include "perception/ui/builders/node.h"
#include "perception/ui/builders/window.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::TextAlignment;
using ::perception::ui::builders::AlignContent;
using ::perception::ui::builders::AlignSelf;
using ::perception::ui::builders::AlignText;
using ::perception::ui::builders::Block;
using ::perception::ui::builders::BorderRadius;
using ::perception::ui::builders::BorderWidth;
using ::perception::ui::builders::FillColor;
using ::perception::ui::builders::FlexDirection;
using ::perception::ui::builders::FlexGrow;
using ::perception::ui::builders::Height;
using ::perception::ui::builders::JustifyContent;
using ::perception::ui::builders::Label;
using ::perception::ui::builders::Margin;
using ::perception::ui::builders::Node;
using ::perception::ui::builders::OnPush;
using ::perception::ui::builders::OnWindowClose;
using ::perception::ui::builders::Padding;
using ::perception::ui::builders::StandardButton;
using ::perception::ui::builders::Text;
using ::perception::ui::builders::Width;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowTitle;

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

template <typename... Modifiers>
std::shared_ptr<perception::ui::Node> CalculatorButton(
    std::string_view label, std::function<void()> on_click_handler,
    Modifiers... modifier) {
  return StandardButton(Label(Text(label)), OnPush(on_click_handler),
                        Margin(YGEdgeHorizontal, 5.0f), modifier...);
}

}  // namespace

int main(int argc, char* argv[]) {
  auto display_node = Label(AlignText(TextAlignment::MiddleCenter),
                            AlignSelf(YGAlignStretch), FlexGrow(1.0));
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

  auto terminal_container =
      Block(FillColor(kTerminalBackgroundColor), BorderWidth(0.0f),
            BorderRadius(0.0f), FlexGrow(1.0f), AlignSelf(YGAlignStretch),
            Margin(YGEdgeAll, 0.0f), display_node);
  /*
    window->OnResize([](float width, float height) {
      bool is_landscape = width > height;
      if (auto top_level_widget =
              Widget::GetWidgetWithId(kTopLevelWidgetId).lock()) {
        top_level_widget->SetFlexDirection(
            is_landscape ? YGFlexDirectionRowReverse : YGFlexDirectionColumn);
      }
    });*/

  auto button_panel = Block(
      FillColor(kButtonPanelBackgroundColor), BorderWidth(0.0f),
      BorderRadius(0.0f), Width(194.0f), AlignSelf(YGAlignStretch),
      Margin(YGEdgeAll, 0.0f), AlignContent(YGAlignCenter),
      JustifyContent(YGJustifyCenter),
      Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
           JustifyContent(YGJustifyCenter), CalculatorButton("C", PressClear),
           CalculatorButton("+-", PressFlipSign),
           CalculatorButton("/", std::bind(PressOperator, DIVIDE)),
           CalculatorButton("x", std::bind(PressOperator, MULTIPLY))),
      Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
           JustifyContent(YGJustifyCenter),
           CalculatorButton("7", std::bind(PressNumber, 7)),
           CalculatorButton("8", std::bind(PressNumber, 8)),
           CalculatorButton("9", std::bind(PressNumber, 9)),
           CalculatorButton("-", std::bind(PressOperator, SUBTRACT))),
      Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
           JustifyContent(YGJustifyCenter),
           CalculatorButton("4", std::bind(PressNumber, 4)),
           CalculatorButton("5", std::bind(PressNumber, 5)),
           CalculatorButton("6", std::bind(PressNumber, 6)),
           CalculatorButton("+", std::bind(PressOperator, ADD))),
      Node(
          FlexDirection(YGFlexDirectionRow), JustifyContent(YGJustifyCenter),
          Node(FlexDirection(YGFlexDirectionColumn),
               Node(FlexDirection(YGFlexDirectionRow),
                    Margin(YGEdgeVertical, 5.0f),
                    JustifyContent(YGJustifyCenter),
                    CalculatorButton("1", std::bind(PressNumber, 1)),
                    CalculatorButton("2", std::bind(PressNumber, 2)),
                    CalculatorButton("3", std::bind(PressNumber, 3))),
               Node(FlexDirection(YGFlexDirectionRow),
                    JustifyContent(YGJustifyCenter),
                    Margin(YGEdgeVertical, 5.0f),
                    CalculatorButton("0", std::bind(PressNumber, 0), Width(58)),
                    CalculatorButton(".", PressDecimal))),
          CalculatorButton("=", std::bind(PressEquals), Height(58),
                           Margin(YGEdgeVertical, 5.0f))));

  auto window = Window(
      WindowTitle("Calculator"), OnWindowClose([]() { TerminateProcess(); }),
      FlexGrow(1.0), FlexDirection(YGFlexDirectionRow),
      Padding(YGEdgeAll, 0.0f), button_panel, terminal_container);

  HandOverControl();
  return 0;
}
