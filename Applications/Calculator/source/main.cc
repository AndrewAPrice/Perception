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
#include "perception/ui/fixed_grid.h"
#include "perception/ui/text_box.h"
#include "perception/ui/ui_window.h"
#include "perception/ui/vertical_container.h"
#include "perception/ui/text_alignment.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::kFillParent;
using ::perception::ui::Button;
using ::perception::ui::FixedGrid;
using ::perception::ui::TextAlignment;
using ::perception::ui::TextBox;
using ::perception::ui::UiWindow;
using ::perception::ui::VerticalContainer;

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
		SetValue("")->SetWidth(kFillParent)->SetHeight(50)->ToSharedPtr());

	window->SetRoot(
		std::make_shared<VerticalContainer>()->
			AddChild(display)->
			AddChild(std::make_shared<FixedGrid>()->SetColumns(4)->SetRows(5)->
				AddChildren({
					std::make_shared<Button>()->SetLabel("C")->
						OnClick(PressClear)->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("+-")->
						OnClick(PressFlipSign)->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("/")->
						OnClick(std::bind(PressOperator, DIVIDE))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("x")->
						OnClick(std::bind(PressOperator, MULTIPLY))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("7")->
						OnClick(std::bind(PressNumber, 7))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("8")->
						OnClick(std::bind(PressNumber, 8))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("9")->
						OnClick(std::bind(PressNumber, 9))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("-")->
						OnClick(std::bind(PressOperator, SUBTRACT))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("4")->
						OnClick(std::bind(PressNumber, 4))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("5")->
						OnClick(std::bind(PressNumber, 5))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("6")->
						OnClick(std::bind(PressNumber, 6))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("+")->
						OnClick(std::bind(PressOperator, ADD))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("1")->
						OnClick(std::bind(PressNumber, 1))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("2")->
						OnClick(std::bind(PressNumber, 2))->ToSharedPtr(),
					std::make_shared<Button>()->SetLabel("3")->
						OnClick(std::bind(PressNumber, 3))->ToSharedPtr()})->
				AddChild(
					std::make_shared<Button>()->SetLabel("=")->
						OnClick(PressEquals)->ToSharedPtr(),
						/*x=*/3, /*y=*/3, /*columns=*/1, /*rows=*/2)->
				AddChild(std::make_shared<Button>()->SetLabel("0")->
						OnClick(std::bind(PressNumber, 0))->ToSharedPtr(),
						/*x=*/0, /*y=*/4, /*columns=*/2, /*rows=*/1)->
				AddChild(std::make_shared<Button>()->SetLabel(".")->
						OnClick(PressDecimal)->ToSharedPtr())
				->SetSize(kFillParent)->ToSharedPtr()
			)->SetMargin(8)->SetSize(kFillParent)->ToSharedPtr())->
		OnClose([]() { TerminateProcess(); });

	HandOverControl();
	return 0;
}
