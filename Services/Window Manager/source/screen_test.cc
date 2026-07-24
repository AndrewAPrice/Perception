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

#include "screen.h"

#include "perception/devices/graphics_device.h"
#include "perception/ui/size.h"
#include "testing.h"

namespace {

using ::perception::ui::Size;
namespace graphics = ::perception::devices::graphics;

TEST(ScreenInitializationAndQueries) {
  InitializeScreen();

  Size size = GetScreenSize();
  EXPECT(1920.0f, size.width);
  EXPECT(1080.0f, size.height);

  size_t texture_id = GetWindowManagerTextureId();
  EXPECT(1, static_cast<int>(texture_id));

  uint32* buffer = GetWindowManagerTextureData();
  EXPECT(true, buffer != nullptr);
}

TEST(ScreenDrawingCommandsAndSynchronization) {
  InitializeScreen();

  // Initially not drawing, sleep check should be non-blocking no-op
  SleepUntilWeAreReadyToStartDrawing();

  graphics::Commands commands;
  RunDrawCommands(commands);

  // After completion, screen drawing should reset to false
  SleepUntilWeAreReadyToStartDrawing();
}

}  // namespace
