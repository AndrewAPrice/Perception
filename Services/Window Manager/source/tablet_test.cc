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

#include "tablet.h"

#include "mouse.h"
#include "perception/ui/point.h"
#include "screen.h"
#include "testing.h"

namespace {

using ::perception::ui::Point;

TEST(TabletInitializationAndPosition) {
  InitializeScreen();
  InitializeMouse();
  InitializeTablets();

  // Test setting position via absolute coordinates
  SetMousePosition({.x = 100.0f, .y = 200.0f});
  Point mouse_pos = GetMousePosition();
  EXPECT(100.0f, mouse_pos.x);
  EXPECT(200.0f, mouse_pos.y);
}

}  // namespace
