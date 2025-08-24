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
#include "perception/devices/mouse_listener.h"
#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"

struct MouseButtonEvent {
  ::perception::devices::MouseButton button;
  bool is_pressed_down;
};

void InitializeMouse();

const ::perception::ui::Point& GetMousePosition();

// Preps the overlays for drawing, which will mark which areas need to be drawn
// to the window manager's texture and not directly to the screen.
void DrawMouse(const ::perception::ui::Rectangle& draw_area);
