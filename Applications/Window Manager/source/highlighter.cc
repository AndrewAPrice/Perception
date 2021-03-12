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

#include "highlighter.h"

#include "compositor.h"
#include "perception/draw.h"
#include "screen.h"

using ::permebuf::perception::devices::GraphicsCommand;
using ::permebuf::perception::devices::GraphicsDriver;

namespace {

bool highlighter_enabled;
int highlighter_min_x;
int highlighter_min_y;
int highlighter_max_x;
int highlighter_max_y;

}

void InitializeHighlighter() {
	highlighter_enabled = false;
}

void SetHighlighter(int min_x, int min_y, int max_x, int max_y) {
	if (highlighter_enabled) {
		if (highlighter_min_x == min_x &&
			highlighter_max_x == max_x &&
			highlighter_min_y == min_y &&
			highlighter_max_y == max_y) {
			return;
		}

		InvalidateScreen(highlighter_min_x, highlighter_min_y,
			highlighter_max_x, highlighter_max_y);
	}

	highlighter_min_x = min_x;
	highlighter_min_y = min_y;
	highlighter_max_x = max_x;
	highlighter_max_y = max_y;
	highlighter_enabled = true;

	InvalidateScreen(highlighter_min_x, highlighter_min_y,
		highlighter_max_x, highlighter_max_y);
}

void DisableHighlighter() {
	if (!highlighter_enabled)
		return;

	highlighter_enabled = false;
	InvalidateScreen(highlighter_min_x, highlighter_min_y,
		highlighter_max_x, highlighter_max_y);
}

void PrepHighlighterForDrawing(int min_x, int min_y, int max_x, int max_y) {
	if (!highlighter_enabled)
		return;
	if (min_x >= highlighter_max_x ||
		min_y >= highlighter_max_y ||
		max_x <= highlighter_min_x ||
		max_y <= highlighter_min_y) {
		// The highlighting is outside of the draw area.
		return;
	}

	CopySectionOfScreenIntoWindowManagersTexture(
		std::max(min_x, highlighter_min_x),
		std::max(min_y, highlighter_min_y),
		std::min(max_x, highlighter_max_x),
		std::min(max_y, highlighter_max_y));
}

void DrawHighlighter(
	Permebuf<GraphicsDriver::RunCommandsMessage>& commands,
	PermebufListOfOneOfs<GraphicsCommand>& last_graphics_command,
	int min_x, int min_y, int max_x, int max_y) {
	if (!highlighter_enabled)
		return;
	if (min_x >= highlighter_max_x ||
		min_y >= highlighter_max_y ||
		max_x <= highlighter_min_x ||
		max_y <= highlighter_min_y) {
		// The highlighting is outside of the draw area.
		return;
	}

	if (!last_graphics_command.IsValid()) {
		// First graphics command. Set the window manager's texture as
		// the destination texture.
		last_graphics_command = commands->MutableCommands();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_graphics_command.Set(command_one_of);
		command_one_of.MutableSetDestinationTexture()
			.SetTexture(GetWindowManagerTextureId());
	}

	// Draw the highlighting cursor.
	last_graphics_command = last_graphics_command.InsertAfter();
	auto draw_command_oneof = commands.AllocateOneOf<GraphicsCommand>();
	last_graphics_command.Set(draw_command_oneof);
	auto fill_rectangle_with_alpha =
		draw_command_oneof.MutableFillRectangle();
	fill_rectangle_with_alpha.SetLeft(std::max(min_x, highlighter_min_x));
	fill_rectangle_with_alpha.SetTop(std::max(min_y, highlighter_min_y));
	fill_rectangle_with_alpha.SetRight(std::min(max_x, highlighter_max_x));
	fill_rectangle_with_alpha.SetBottom(std::min(max_y, highlighter_max_y));
	fill_rectangle_with_alpha.SetColor(HIGHLIGHTER_TINT);
}