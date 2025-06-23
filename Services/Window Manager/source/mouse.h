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

#include "perception/devices/graphics_device.h"

void InitializeMouse();
int GetMouseX();
int GetMouseY();

// Preps the overlays for drawing, which will mark which areas need to be drawn
// to the window manager's texture and not directly to the screen.
void PrepMouseForDrawing(int min_x, int min_y, int max_x, int max_y);

// Draws the mouse.
void DrawMouse(
    ::perception::devices::graphics::Commands& commands,
    int min_x, int min_y, int max_x, int max_y);
