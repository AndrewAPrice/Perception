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

#include "compositor_quad_tree.h"
#include "frame.h"
#include "highlighter.h"
#include "mouse.h"
#include "perception/draw.h"
#include "perception/object_pool.h"
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

CompositorQuadTree quad_tree;

}

void DrawBackground(int min_x, int min_y, int max_x, int max_y) {
	DrawSolidColor(min_x, min_y, max_x, max_y, kBackgroundColor);
}

void InitializeCompositor() {
	has_invalidated_area = false;
	quad_tree = CompositorQuadTree();
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
}

void DrawScreen() {
	if (!has_invalidated_area)
		return;

	SleepUntilWeAreReadyToStartDrawing();

	has_invalidated_area = false;
	int min_x = std::max(0, invalidated_area_min_x);
	int min_y = std::max(0, invalidated_area_min_y);
	int max_x = std::min(GetScreenWidth(), invalidated_area_max_x);
	int max_y = std::min(GetScreenHeight(), invalidated_area_max_y);

	DrawBackground(min_x, min_y, max_x, max_y);

	Frame* root_frame = Frame::GetRootFrame();
	if (root_frame)
		root_frame->Draw(min_x, min_y, max_x, max_y);

	Window::ForEachBackToFrontDialog([&](Window& window) {
		window.Draw(min_x, min_y, max_x, max_y);
	});

	// Prep the overlays for drawing, which will mark which areas need to be
	// drawn to the window manager's texture and not directly to the screen.
	PrepHighlighterForDrawing(min_x, min_y, max_x, max_y);
	PrepMouseForDrawing(min_x, min_y, max_x, max_y);

	// Draw the screen.
	Permebuf<GraphicsDriver::RunCommandsMessage> commands;

	// There are 3 stages of commands that we want to construct:
	// (1) Draw any rectangles into the WM Texture.
	// (2) Draw mouse/highlighter into the WM Texture.
	// (3) Draw WM textures into framebuffer.
	// (4) Draw window textures into framebuffer.

	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_into_wm_texture_command, last_draw_into_wm_texture_command;
	bool has_commands_to_draw_into_wm_texture = false;
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_wm_texture_into_framebuffer_command,
		last_draw_wm_texture_into_framebuffer_command;
	bool has_commands_to_draw_wm_into_frame_buffer = false;
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		first_draw_into_framebuffer_command,
		last_draw_into_framebuffer_command;
	bool has_commands_to_draw_into_framebuffer = false;	

	size_t texture_drawing_into_window_manager = 0;
	size_t texture_drawing_into_framebuffer = 0;

	quad_tree.ForEachItem([&](Rectangle* rectangle) {
		if (rectangle->draw_into_wm_texture) {
			// Draw this into the WM texture.
			if (rectangle->texture_id != GetWindowManagerTextureId()) {
				// This is NOT the window manager texture, which is already
				// applied to the window manager texture.

				// Draw this rectangle into the window manager's texture.
				if (has_commands_to_draw_into_wm_texture) {
					last_draw_into_wm_texture_command =
						last_draw_into_wm_texture_command.InsertAfter();
				} else {
					first_draw_into_wm_texture_command = 
						last_draw_into_wm_texture_command =
						commands.AllocateListOfOneOfs<
							::permebuf::perception::devices::GraphicsCommand>();

					has_commands_to_draw_into_wm_texture = true;
				}

				if (rectangle->IsSolidColor()) {
					// Draw solid color into window manager texture.
					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_wm_texture_command.Set(
						command_one_of);
					auto command = command_one_of.MutableFillRectangle();
					command.SetLeft(rectangle->min_x);
					command.SetTop(rectangle->min_y);
					command.SetRight(rectangle->max_x);
					command.SetBottom(rectangle->max_y);
					command.SetColor(rectangle->color);
				} else {
					// Copy texture into window manager's texture.
					if (rectangle->texture_id !=
						texture_drawing_into_window_manager) {
						// Switch over to source texture.
						texture_drawing_into_window_manager =
							rectangle->texture_id;

						auto command_one_of =
							commands.AllocateOneOf<GraphicsCommand>();
						last_draw_into_wm_texture_command.Set(
							command_one_of);
						command_one_of.MutableSetSourceTexture()
							.SetTexture(rectangle->texture_id);

						last_draw_into_wm_texture_command =
							last_draw_into_wm_texture_command.
								InsertAfter();
					}

					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_wm_texture_command.Set(
						command_one_of);
					auto command = command_one_of.MutableCopyPartOfATexture();
					command.SetLeftSource(rectangle->texture_x);
					command.SetTopSource(rectangle->texture_y);
					command.SetLeftDestination(rectangle->min_x);
					command.SetTopDestination(rectangle->min_y);
					command.SetWidth(rectangle->max_x - rectangle->min_x);
					command.SetHeight(rectangle->max_y - rectangle->min_y);
				}
			}

			// Now copy this area from the Window Manager's texture into the
			// framebuffer.
			if (has_commands_to_draw_wm_into_frame_buffer) {
				last_draw_wm_texture_into_framebuffer_command =
					last_draw_wm_texture_into_framebuffer_command.
						InsertAfter();
			} else {
				first_draw_wm_texture_into_framebuffer_command =
					last_draw_wm_texture_into_framebuffer_command =
					commands.AllocateListOfOneOfs<
						::permebuf::perception::devices::GraphicsCommand>();
				has_commands_to_draw_wm_into_frame_buffer = true;
			}

			auto command_one_of =
				commands.AllocateOneOf<GraphicsCommand>();
			last_draw_wm_texture_into_framebuffer_command.Set(
				command_one_of);
			auto command = command_one_of.MutableCopyPartOfATexture();
			command.SetLeftSource(rectangle->min_x);
			command.SetTopSource(rectangle->min_y);
			command.SetLeftDestination(rectangle->min_x);
			command.SetTopDestination(rectangle->min_y);
			command.SetWidth(rectangle->max_x - rectangle->min_x);
			command.SetHeight(rectangle->max_y - rectangle->min_y);
		} else {
			// Draw this rectangle straight into the framebuffer.
			if (has_commands_to_draw_into_framebuffer) {
				last_draw_into_framebuffer_command =
					last_draw_into_framebuffer_command.
						InsertAfter();
			} else {
				first_draw_into_framebuffer_command =
					last_draw_into_framebuffer_command =
					commands.AllocateListOfOneOfs<
						::permebuf::perception::devices::GraphicsCommand>();
				has_commands_to_draw_into_framebuffer = true;
			}

			if (rectangle->IsSolidColor()) {
				// Draw solid color into framebuffer.
				auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
				last_draw_into_framebuffer_command.Set(
					command_one_of);
				auto command = command_one_of.MutableFillRectangle();
				command.SetLeft(rectangle->min_x);
				command.SetTop(rectangle->min_y);
				command.SetRight(rectangle->max_x);
				command.SetBottom(rectangle->max_y);
				command.SetColor(rectangle->color);
			} else {
				// Copy texture into framebuffer.
				if (rectangle->texture_id != texture_drawing_into_framebuffer) {
					// Switch over the source texture.
					texture_drawing_into_framebuffer = rectangle->texture_id;

					auto command_one_of =
						commands.AllocateOneOf<GraphicsCommand>();
					last_draw_into_framebuffer_command.Set(
						command_one_of);
					command_one_of.MutableSetSourceTexture()
						.SetTexture(rectangle->texture_id);

					last_draw_into_framebuffer_command =
						last_draw_into_framebuffer_command.
							InsertAfter();
				}

				auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
				last_draw_into_framebuffer_command.Set(
					command_one_of);
				auto command = command_one_of.MutableCopyPartOfATexture();
				command.SetLeftSource(rectangle->texture_x);
				command.SetTopSource(rectangle->texture_y);
				command.SetLeftDestination(rectangle->min_x);
				command.SetTopDestination(rectangle->min_y);
				command.SetWidth(rectangle->max_x - rectangle->min_x);
				command.SetHeight(rectangle->max_y - rectangle->min_y);
			}
		}
	 });

	// Merge all the draw commands together.
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>
		last_draw_command;

	if (has_commands_to_draw_into_wm_texture) {
		// We have things to draw into the window manager's texture.

		// Set destination to be the wm texture.
		last_draw_command = commands->MutableCommands();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_draw_command.Set(command_one_of);
		command_one_of.MutableSetDestinationTexture()
			.SetTexture(GetWindowManagerTextureId());

		// Chain the commands onto the end.
		last_draw_command.SetNext(first_draw_into_wm_texture_command);
		last_draw_command = last_draw_into_wm_texture_command;
	}

	// Draw some overlays.
	DrawHighlighter(commands, last_draw_command, min_x, min_y, max_x,
		max_y);
	DrawMouse(commands, last_draw_command, min_x, min_y, max_x, max_y);

	// Set the destination to be the framebuffer.
	if (last_draw_command.IsValid()) {
		last_draw_command = last_draw_command.InsertAfter();
	} else {
		last_draw_command = commands->MutableCommands();
	}
	auto set_destination_to_framebuffer_command_one_of =
		commands.AllocateOneOf<GraphicsCommand>();
	last_draw_command.Set(set_destination_to_framebuffer_command_one_of);
	set_destination_to_framebuffer_command_one_of
		.MutableSetDestinationTexture().SetTexture(0);  // The screen.

	if (has_commands_to_draw_wm_into_frame_buffer) {
		// Set the source to be the window manager's texture.
		last_draw_command = last_draw_command.InsertAfter();
		auto command_one_of = commands.AllocateOneOf<GraphicsCommand>();
		last_draw_command.Set(command_one_of);
		command_one_of.MutableSetSourceTexture().SetTexture(
			GetWindowManagerTextureId());

		// Chain the commands onto the end.
		last_draw_command.SetNext(
			first_draw_wm_texture_into_framebuffer_command);
		last_draw_command =
			last_draw_wm_texture_into_framebuffer_command;
	}

	if (has_commands_to_draw_into_framebuffer) {
		// Chain the commands onto the end.
		last_draw_command.SetNext(first_draw_into_framebuffer_command);
		last_draw_command = last_draw_into_framebuffer_command;
	}

	RunDrawCommands(std::move(commands));

	// Reset the quad tree.
	quad_tree.Reset();
}


// Draws a solid color on the screen.
void DrawSolidColor(int min_x, int min_y, int max_x, int max_y,
	uint32 fill_color) {
	Rectangle* rectangle = quad_tree.AllocateRectangle();
	rectangle->min_x = min_x;
	rectangle->min_y = min_y;
	rectangle->max_x = max_x;
	rectangle->max_y = max_y;
	rectangle->texture_id = 0;
	rectangle->color = fill_color;
	rectangle->draw_into_wm_texture = false;
	quad_tree.AddOccludingRectangle(rectangle);
}

// Copies a part of a texture onto the screen.
void CopyTexture(int min_x, int min_y, int max_x, int max_y, size_t texture_id,
	int texture_x, int texture_y) {
	Rectangle* rectangle = quad_tree.AllocateRectangle();
	rectangle->min_x = min_x;
	rectangle->min_y = min_y;
	rectangle->max_x = max_x;
	rectangle->max_y = max_y;
	rectangle->texture_id = texture_id;
	rectangle->texture_x = texture_x;
	rectangle->texture_y = texture_y;

	if (texture_id == GetWindowManagerTextureId()) {
		// We're copying from the window manager's texture, so we can consider
		// the same as textures that we are going to draw into the window
		// manager.
		rectangle->draw_into_wm_texture = true;
	} else {
		rectangle->draw_into_wm_texture = false;
	}
	quad_tree.AddOccludingRectangle(rectangle);
}

void CopySectionOfScreenIntoWindowManagersTexture(int min_x, int min_y,
	int max_x, int max_y) {
	quad_tree.DrawAreaToWindowManagerTexture(min_x, min_y, max_x, max_y);	
}