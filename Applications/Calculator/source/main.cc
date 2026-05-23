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

#include "include/core/SkFont.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_container.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::GetBook12UiFont;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollContainer;
using ::perception::ui::components::UiWindow;

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
bool just_calculated = false;
bool scroll_to_bottom_requested = false;

std::shared_ptr<Label> display;
std::shared_ptr<Node> history_content;
std::shared_ptr<ScrollContainer> history_scroll_view;

SkFont* GetBook10UiFont() {
  static std::unique_ptr<SkFont> book_10_ui_font;
  if (!book_10_ui_font) {
    book_10_ui_font = std::make_unique<SkFont>(*GetBook12UiFont());
    book_10_ui_font->setSize(10.0f);
  }
  return book_10_ui_font.get();
}

std::string GetOperatorString(Operation op) {
  switch (op) {
    case ADD:
      return "+";
    case SUBTRACT:
      return "-";
    case DIVIDE:
      return "/";
    case MULTIPLY:
      return "x";
    default:
      return "";
  }
}

std::string FormatFinishedNumber(double number) {
  std::stringstream buffer;
  buffer << number;
  return buffer.str();
}

std::string FormatCurrentNumber() {
  std::stringstream buffer;
  buffer << current_number;
  if (decimal_pressed && decimal_multiplier == 0.1) {
    return buffer.str() + ".";
  } else {
    return buffer.str();
  }
}

void UpdateDisplay() {
  if (operation == NOTHING) {
    display->SetText(FormatCurrentNumber());
  } else {
    std::string expr =
        FormatFinishedNumber(last_number) + " " + GetOperatorString(operation);
    if (any_number) {
      expr += " " + FormatCurrentNumber();
    }
    display->SetText(expr);
  }
}

void AddToHistory(const std::string& expression, const std::string& result) {
  auto entry = Container::VerticalContainer(
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetGap(2.0f);
        layout.SetPadding(YGEdgeBottom, 6.0f);
      },
      Label::BasicLabel(
          expression,
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleLeft);
            label.SetFont(GetBook10UiFont());
            label.SetColor(SkColorSetARGB(0xFF, 0x77, 0x77, 0x77));
          },
          [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); }),
      Label::BasicLabel(
          result,
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleRight);
            label.SetFont(GetBook12UiFont());
          },
          [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); }));

  history_content->AddChild(entry);
  scroll_to_bottom_requested = true;
}

void PressNumber(int number) {
  if (just_calculated) {
    current_number = 0.0;
    any_number = false;
    decimal_pressed = false;
    just_calculated = false;
  }

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
  if (just_calculated) {
    just_calculated = false;
    any_number = true;
  }
  current_number *= -1;
  UpdateDisplay();
}

void PressDecimal() {
  if (just_calculated) {
    current_number = 0.0;
    any_number = false;
    decimal_pressed = false;
    just_calculated = false;
  }

  if (decimal_pressed) return;

  decimal_pressed = true;
  decimal_multiplier = 0.1;

  if (!any_number) {
    current_number = 0.0;
  }
  UpdateDisplay();
}

void PressEquals() {
  if (operation == NOTHING) return;

  std::string expr = FormatFinishedNumber(last_number) + " " +
                     GetOperatorString(operation) + " " + FormatCurrentNumber();
  double result = 0.0;

  switch (operation) {
    case ADD:
      result = last_number + current_number;
      break;
    case SUBTRACT:
      result = last_number - current_number;
      break;
    case DIVIDE:
      if (current_number != 0.0) {
        result = last_number / current_number;
      }
      break;
    case MULTIPLY:
      result = last_number * current_number;
      break;
    default:
      return;
  }

  AddToHistory(expr, FormatFinishedNumber(result));

  current_number = result;
  operation = NOTHING;
  any_number = false;
  decimal_pressed = false;
  just_calculated = true;
  UpdateDisplay();
}

void PressOperator(Operation new_operator) {
  if (operation != NOTHING) PressEquals();

  operation = new_operator;
  last_number = current_number;
  any_number = false;
  decimal_pressed = false;
  current_number = 0.0;
  just_calculated = false;
  UpdateDisplay();
}

void PressClear() {
  current_number = 0.0;
  last_number = 0.0;
  operation = NOTHING;
  any_number = false;
  decimal_pressed = false;
  just_calculated = false;
  UpdateDisplay();
}

}  // namespace

int main(int argc, char* argv[]) {
  history_content = Container::VerticalContainer([](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
    layout.SetJustifyContent(YGJustifyFlexEnd);
    layout.SetWidthPercent(100.0f);
  });

  history_content->OnDrawPostChildren([](const DrawContext& context) {
    if (scroll_to_bottom_requested) {
      float max_y = history_scroll_view->ContentSize().height -
                    history_scroll_view->ContainerSize().height;
      if (max_y < 0.0f) max_y = 0.0f;
      history_scroll_view->SetContentPosition({.x = 0.0f, .y = max_y});
      scroll_to_bottom_requested = false;
    }
  });

  std::shared_ptr<Node> history_scroll_container =
      ScrollContainer::VerticalScrollContainer(
          history_content, &history_scroll_view, [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetPadding(YGEdgeAll, 8.0f);
          });

  auto terminal_container = Block::SolidColor(
      kTerminalBackgroundColor,
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetMargin(YGEdgeAll, 0.f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetGap(0.0f);
          },
          history_scroll_container,
          Block::SolidColor(SkColorSetARGB(0xFF, 0xE0, 0xE0, 0xE0),
                            [](Layout& layout) {
                              layout.SetAlignSelf(YGAlignStretch);
                              layout.SetHeight(1.0f);
                            }),
          Label::BasicLabel(
              "",
              [](Label& label) {
                label.SetTextAlignment(TextAlignment::MiddleRight);
              },
              [](Layout& layout) {
                layout.SetAlignSelf(YGAlignStretch);
                layout.SetHeight(40.0f);
                layout.SetPadding(YGEdgeHorizontal, 12.0f);
              },
              &display)));

  auto button_panel = Block::SolidColor(
      kButtonPanelBackgroundColor,
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetAlignContent(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
        // Flush this button panel against the sides of the window.
        for (auto edge : {YGEdgeTop, YGEdgeBottom, YGEdgeLeft})
          layout.SetMargin(edge, -8.0f);
        layout.SetPadding(YGEdgeAll, 8.0f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetAlignSelf(YGAlignCenter);
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

  auto window = UiWindow::ResizableWindowWithTitleBar(
      "Calculator",
      [](UiWindow& window) { window.OnClose([]() { TerminateProcess(); }); },
      Container::HorizontalContainer(
          [](Layout& layout) { layout.SetFlexGrow(1.0); }, button_panel,
          terminal_container));

  HandOverControl();
  return 0;
}
