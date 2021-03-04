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

#include "compositor.h"

#include "frame.h"
#include "highlighter.h"
#include "mouse.h"
#include "perception/draw.h"
#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"
#include "screen.h"
#include "types.h"
#include "window.h"

using ::perception::FillRectangle;
using ::perception::DrawSprite1bitAlpha;

using ::permebuf::perception::devices::GraphicsDriver;
using ::permebuf::perception::devices::GraphicsCommand;

namespace {

bool has_invalidated_area;
int invalidated_area_min_x;
int invalidated_area_min_y;
int invalidated_area_max_x;
int invalidated_area_max_y;


void DrawBackground(int min_x, int min_y, int max_x, int max_y) {
	FillRectangle(min_x, min_y, max_x, max_y, kBackgroundColor,
		GetWindowManagerTextureData(), GetScreenWidth(), GetScreenHeight());
}

}

void InitializeCompositor() {
	has_invalidated_area = false;
}

void InvalidateScreen(int min_x, int min_y, int max_x, int max_y) {
	if (has_invalidated_area) {
		invalidated_area_min_x = std::min(min_x, invalidated_area_min_x);
		invalidated_area_min_y = std::min(min_y, invalidated_area_min_y);
		invalidated_area_max_x = std::max(max_x, invalidated_area_max_x);
		invalidated_area_max_y = std::max(max_y, invalidated_area_max_y);
	} else {
		invalidated_area_min_x = min_x;
		invalidated_area_min_y = min_y;
		invalidated_area_max_x = max_x;
		invalidated_area_max_y = max_y;
		has_invalidated_area = true;
	}

	invalidated_area_min_x = std::max(0, invalidated_area_min_x);
	invalidated_area_min_y = std::max(0, invalidated_area_min_y);
	invalidated_area_max_x = std::min(invalidated_area_max_x, GetScreenWidth());
	invalidated_area_max_y = std::min(invalidated_area_max_y, GetScreenHeight());
}

void DrawScreen() {
	if (!has_invalidated_area)
		return;

	has_invalidated_area = false;
	int min_x = std::max(0, invalidated_area_min_x);
	int min_y = std::max(0, invalidated_area_min_y);
	int max_x = std::min(GetScreenWidth(), invalidated_area_max_x);
	int max_y = std::min(GetScreenHeight(), invalidated_area_max_y);

	SleepUntilWeAreReadyToStartDrawing();

	DrawBackground(min_x, min_y, max_x, max_y);

	Frame* root_frame = Frame::GetRootFrame();
	if (root_frame)
		root_frame->Draw(min_x, min_y, max_x, max_y);

	Window::ForEachBackToFrontDialog([&](Window& window) {
		window.Draw(min_x, min_y, max_x, max_y);
	});


	// Draw the screen.
	Permebuf<GraphicsDriver::RunCommandsMessage> commands;

	// Set the destination to the Window Manager texture.
	auto last_command = commands->MutableCommands();
	auto set_destination_to_wm_texture_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_command.Set(set_destination_to_wm_texture_oneof);
	set_destination_to_wm_texture_oneof.MutableSetDestinationTexture()
		.SetTexture(GetWindowManagerTextureId());

	// Draw some overlays.
	DrawHighlighter(commands, last_command, min_x, min_y, max_x, max_y);
	DrawMouse(commands, last_command, min_x, min_y, max_x, max_y);

	// Copy the part that has changed.
	last_command = last_command.InsertAfter();
	auto command_1_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_command.Set(command_1_oneof);
	command_1_oneof.MutableSetSourceTexture()
		.SetTexture(GetWindowManagerTextureId());
	last_command = last_command.InsertAfter();
	auto command_2_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_command.Set(command_2_oneof);
	command_2_oneof.MutableSetDestinationTexture()
		.SetTexture(0); // The screen
	last_command = last_command.InsertAfter();
	auto command_3_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_command.Set(command_3_oneof);
	auto command_3_copy_texture = command_3_oneof.MutableCopyPartOfATexture();
	command_3_copy_texture.SetLeftSource(min_x);
	command_3_copy_texture.SetTopSource(min_y);
	command_3_copy_texture.SetLeftDestination(min_x);
	command_3_copy_texture.SetTopDestination(min_y);
	command_3_copy_texture.SetWidth(max_x - min_x);
	command_3_copy_texture.SetHeight(max_y - min_y);

	RunDrawCommands(std::move(commands));
}