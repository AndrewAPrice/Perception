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

#pragma once

#include "permebuf/Libraries/perception/devices/graphics_driver.permebuf.h"

void InitializeHighlighter();
void SetHighlighter(int min_x, int min_y, int max_x, int max_y);
void DisableHighlighter();

// Preps the overlays for drawing, which will mark which areas need to be drawn
// to the window manager's texture and not directly to the screen.
void PrepHighlighterForDrawing(int min_x, int min_y, int max_x, int max_y);

// Draws the highlighter.
void DrawHighlighter(
	Permebuf<::permebuf::perception::devices::GraphicsDriver::RunCommandsMessage>& commands,
	PermebufListOfOneOfs<::permebuf::perception::devices::GraphicsCommand>& last_graphics_command,
	int min_x, int min_y, int max_x, int max_y);