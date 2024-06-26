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
#include "perception/ui/button.h"
#include "perception/ui/container.h"
#include "perception/ui/image_effect.h"
#include "perception/ui/label.h"
#include "perception/ui/parentless_widget.h"
#include "perception/ui/scroll_container.h"
#include "perception/ui/text_alignment.h"
#include "perception/ui/text_box.h"
#include "perception/ui/ui_window.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::Button;
using ::perception::ui::Container;
using ::perception::ui::kFillParent;
using ::perception::ui::ImageEffect;
using ::perception::ui::Label;
using ::perception::ui::ParentlessWidget;
using ::perception::ui::ScrollContainer;
using ::perception::ui::TextAlignment;
using ::perception::ui::TextBox;
using ::perception::ui::UiWindow;
using ::perception::ui::Widget;

namespace {

constexpr size_t kTopLevelWidgetId = 1;
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

std::shared_ptr<TextBox> display;
std::shared_ptr<Widget> terminal_content;

void UpdateDisplay() {
  // std::to_string converts the number to fixed decimal places, which doesn't
  // behave as you'd expect a calculator to.
  std::stringstream buffer;
  buffer << current_number;
  if (decimal_pressed && decimal_multiplier == 0.1) {
    display->SetValue(buffer.str() + ".");
  } else {
    display->SetValue(buffer.str());
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

std::shared_ptr<Widget> CreateButton(std::string_view label,
                                     std::function<void()> on_click_handler) {
  return Button::Create()
      ->SetLabel(label)
      ->OnClick(on_click_handler)
      ->SetMargin(YGEdgeHorizontal, 5.0f)
      ->ToSharedPtr();
}

}  // namespace

int main(int argc, char *argv[]) {
  auto window = std::make_shared<UiWindow>("Calculator");

  display = std::static_pointer_cast<TextBox>(
      TextBox::Create()
          ->SetTextAlignment(TextAlignment::BottomRight)
          ->SetAlignSelf(YGAlignStretch)
          ->SetFlexGrow(1.0)
          ->ToSharedPtr());

  terminal_content = std::make_shared<Widget>()
                         ->SetFlexDirection(YGFlexDirectionColumn)
                         ->ToSharedPtr();
  /*for (int i = 0; i < 80; i++) {
    terminal_content->AddChild(
        std::make_shared<Label>()->SetLabel("Hello")->ToSharedPtr());
  }*/

  auto terminal_container =
      ScrollContainer::Create(ParentlessWidget::Create(terminal_content),
                              /*show_vertical_scroll_bar=*/true,
                              /*show_horizontal_scroll_bar=*/false)

          ->SetBackgroundColor(kTerminalBackgroundColor)
          ->SetBorderWidth(0.0f)
          ->SetBorderRadius(0.0f)
          ->SetFlexGrow(1.0)
          ->SetAlignSelf(YGAlignStretch)
          ->SetMargin(YGEdgeAll, 0.0f)
          ->ToSharedPtr();
  /*
    window->OnResize([](float width, float height) {
      bool is_landscape = width > height;
      if (auto top_level_widget =
              Widget::GetWidgetWithId(kTopLevelWidgetId).lock()) {
        top_level_widget->SetFlexDirection(
            is_landscape ? YGFlexDirectionRowReverse : YGFlexDirectionColumn);
      }
    });*/

  auto button_panel =
      Container::Create()
          ->SetBackgroundColor(kButtonPanelBackgroundColor)
          ->SetBorderWidth(0.0f)
          ->SetBorderRadius(0.0f)
          ->SetWidth(194.0f)
          ->SetAlignSelf(YGAlignStretch)
          ->SetMargin(YGEdgeAll, 0.0f)
          ->SetAlignContent(YGAlignCenter)
          ->SetJustifyContent(YGJustifyCenter)
          ->AddChildren(
              {std::make_shared<Widget>()
                   ->SetFlexDirection(YGFlexDirectionRow)
                   ->SetMargin(YGEdgeVertical, 5.0f)
                   ->AddChildren(
                       {CreateButton("C", PressClear),
                        CreateButton("+-", PressFlipSign),
                        CreateButton("/", std::bind(PressOperator, DIVIDE)),
                        CreateButton("x", std::bind(PressOperator, MULTIPLY))})
                   ->ToSharedPtr(),
               std::make_shared<Widget>()
                   ->SetFlexDirection(YGFlexDirectionRow)
                   ->SetMargin(YGEdgeVertical, 5.0f)
                   ->AddChildren(
                       {CreateButton("7", std::bind(PressNumber, 7)),
                        CreateButton("8", std::bind(PressNumber, 8)),
                        CreateButton("9", std::bind(PressNumber, 9)),
                        CreateButton("-", std::bind(PressOperator, SUBTRACT))})
                   ->ToSharedPtr(),
               std::make_shared<Widget>()
                   ->SetFlexDirection(YGFlexDirectionRow)
                   ->SetMargin(YGEdgeVertical, 5.0f)
                   ->AddChildren(
                       {CreateButton("4", std::bind(PressNumber, 4)),
                        CreateButton("5", std::bind(PressNumber, 5)),
                        CreateButton("6", std::bind(PressNumber, 6)),
                        CreateButton("+", std::bind(PressOperator, ADD))})
                   ->ToSharedPtr(),
               std::make_shared<Widget>()
                   ->SetFlexDirection(YGFlexDirectionRow)
                   ->AddChildren(
                       {std::make_shared<Widget>()
                            ->SetFlexDirection(YGFlexDirectionColumn)
                            ->AddChildren(
                                {std::make_shared<Widget>()
                                     ->SetFlexDirection(YGFlexDirectionRow)
                                     ->SetMargin(YGEdgeVertical, 5.0f)
                                     ->AddChildren(
                                         {CreateButton(
                                              "1", std::bind(PressNumber, 1)),
                                          CreateButton(
                                              "2", std::bind(PressNumber, 2)),
                                          CreateButton(
                                              "3", std::bind(PressNumber, 3))})
                                     ->ToSharedPtr(),
                                 std::make_shared<Widget>()
                                     ->SetFlexDirection(YGFlexDirectionRow)
                                     ->SetMargin(YGEdgeVertical, 5.0f)
                                     ->AddChildren(
                                         {CreateButton(
                                              "0", std::bind(PressNumber, 0))
                                              ->SetWidth(74)
                                              ->ToSharedPtr(),
                                          CreateButton(".", PressDecimal)})
                                     ->ToSharedPtr()})
                            ->ToSharedPtr(),
                        CreateButton("=", std::bind(PressEquals))
                            ->SetHeight(74)
                            ->SetMargin(YGEdgeVertical, 5.0f)
                            ->ToSharedPtr()})
                   ->ToSharedPtr()})
          ->ToSharedPtr();

  window->OnClose([]() { TerminateProcess(); })
      ->SetId(kTopLevelWidgetId)
      ->SetFlexDirection(YGFlexDirectionRow)
      ->SetPadding(YGEdgeAll, 0.0f)
      ->AddChildren({button_panel, terminal_container});
  window->Create();

  HandOverControl();
  return 0;
}
