// Copyright 2026 Google LLC
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

#include "mouse.h"

#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "screen.h"
#include "testing.h"

namespace {

using ::perception::ui::Point;
using ::perception::ui::Rectangle;

TEST(MouseInitializationAndPosition) {
  InitializeScreen();
  InitializeMouse();

  // Screen size in test mode is 1920x1080, so initial mouse position is center (960, 540)
  Point mouse_pos = GetMousePosition();
  EXPECT(960.0f, mouse_pos.x);
  EXPECT(540.0f, mouse_pos.y);
}

TEST(MouseDrawingAndInvalidation) {
  InitializeScreen();
  InitializeMouse();

  // Invalidate current mouse area
  InvalidateMouse();

  // Draw mouse overlay within draw area
  Rectangle draw_area{.origin = {.x = 0.0f, .y = 0.0f},
                      .size = {.width = 1920.0f, .height = 1080.0f}};
  DrawMouse(draw_area);

  // Draw mouse overlay outside draw area (should return early safely)
  Rectangle outside_area{.origin = {.x = 0.0f, .y = 0.0f},
                         .size = {.width = 100.0f, .height = 100.0f}};
  DrawMouse(outside_area);
}

}  // namespace
