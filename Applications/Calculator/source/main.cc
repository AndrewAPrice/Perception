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

#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/ui/button.h"
#include "perception/ui/label.h"
#include "perception/ui/text_box.h"
#include "perception/ui/ui_window.h"
#include "perception/ui/text_alignment.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::kFillParent;
using ::perception::ui::Button;
using ::perception::ui::Label;
using ::perception::ui::TextAlignment;
using ::perception::ui::TextBox;
using ::perception::ui::UiWindow;
using ::perception::ui::Widget;
namespace {
enum Operation {
	NOTHING = 0,
	ADD = 1,
	SUBTRACT = 2,
	DIVIDE = 3,
	MULTIPLY = 4
};
Operation operation = NOTHING;
double last_number = 0.0;
double current_number = 0.0;
bool any_number = false;
bool decimal_pressed = false;
double decimal_multiplier = 0.1;

std::shared_ptr<TextBox> display;

void UpdateDisplay() {
	// std::to_string converts the number to fixed decimal places, which doesn't
	// behave as you'd expect a calculator to.
	std::stringstream buffer;
	buffer << current_number;
	if (decimal_pressed && decimal_multiplier == 0.1) {
		display->SetValue(
			buffer.str() + ".");
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
	if (decimal_pressed)
		return;

	decimal_pressed = true;
	decimal_multiplier = 0.1;

	if (!any_number) {
		current_number = 0.0;
	}
	UpdateDisplay();
}

void PressEquals() {
	switch (operation) {
		case ADD:
			current_number = last_number + current_number;
			break;
		case SUBTRACT:
			current_number = last_number - current_number;
			break;
		case DIVIDE:
			if (current_number != 0)
				current_number = last_number / current_number;
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
	if (operation != NOTHING)
		PressEquals();

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

}

int main() {
	auto window = std::make_shared<UiWindow>("Calculator");
	display = std::static_pointer_cast<TextBox>(
		std::make_shared<TextBox>()->
		SetTextAlignment(TextAlignment::MiddleRight)->
		SetValue("")->SetHeight(50)->SetFlexGrow(0.2)->ToSharedPtr());

	window->
		OnClose([]() { TerminateProcess(); })->
		SetFlexDirection(YGFlexDirectionColumn)->
		AddChild(display)->
		AddChild(std::make_shared<Widget>()->
			SetFlexGrow(0.8)->
			SetFlexWrap(YGWrapWrap)->
			AddChildren({
				std::make_shared<Button>()->
					OnClick(PressClear)->AddChild(
						std::make_shared<Label>()->SetLabel("C")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(PressFlipSign)->AddChild(
						std::make_shared<Label>()->SetLabel("+-")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressOperator, DIVIDE))->AddChild(
						std::make_shared<Label>()->SetLabel("/")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressOperator, MULTIPLY))->AddChild(
						std::make_shared<Label>()->SetLabel("x")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 7))->AddChild(
						std::make_shared<Label>()->SetLabel("7")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 8))->AddChild(
						std::make_shared<Label>()->SetLabel("8")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 9))->AddChild(
						std::make_shared<Label>()->SetLabel("9")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressOperator, SUBTRACT))->AddChild(
						std::make_shared<Label>()->SetLabel("-")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 4))->AddChild(
						std::make_shared<Label>()->SetLabel("4")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 5))->AddChild(
						std::make_shared<Label>()->SetLabel("5")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 6))->AddChild(
						std::make_shared<Label>()->SetLabel("6")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressOperator, ADD))->AddChild(
						std::make_shared<Label>()->SetLabel("+")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 1))->AddChild(
						std::make_shared<Label>()->SetLabel("1")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 2))->AddChild(
						std::make_shared<Label>()->SetLabel("2")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 3))->AddChild(
						std::make_shared<Label>()->SetLabel("3")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(PressEquals)->AddChild(
						std::make_shared<Label>()->SetLabel("=")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(40)->
					SetPosition(YGEdgeRight, 0)->SetPosition(YGEdgeRight, 0)->
					SetPositionType(YGPositionTypeAbsolute)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(std::bind(PressNumber, 0))->AddChild(
						std::make_shared<Label>()->SetLabel("0")->ToSharedPtr())->
					SetWidthPercent(50)->SetHeightPercent(20)->ToSharedPtr(),
				std::make_shared<Button>()->
					OnClick(PressDecimal)->AddChild(
						std::make_shared<Label>()->SetLabel(".")->ToSharedPtr())->
					SetWidthPercent(25)->SetHeightPercent(20)->ToSharedPtr()})
			->ToSharedPtr()
		)->SetMargin(YGEdgeAll, 8);

	HandOverControl();
	return 0;
}
