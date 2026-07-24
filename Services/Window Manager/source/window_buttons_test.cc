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

#include "window_buttons.h"

#include <optional>

#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "testing.h"
#include "window_manager.h"

namespace {

using ::perception::ui::Point;
using ::perception::ui::Size;

TEST(WindowButtonSizeCalculation) {
  // Default scale is 1.0f
  Size resizable_size = WindowButtonSize(/*is_resizable=*/true);
  EXPECT(60.0f, resizable_size.width);
  EXPECT(24.0f, resizable_size.height);

  Size non_resizable_size = WindowButtonSize(/*is_resizable=*/false);
  EXPECT(42.0f, non_resizable_size.width);  // 60 - 18 = 42
  EXPECT(24.0f, non_resizable_size.height);
}

TEST(WindowButtonTextureOffsetCalculation) {
  // Unselected button offsets
  Point resizable_none = WindowButtonTextureOffset(
      /*is_resizable=*/true, std::nullopt);
  EXPECT(0.0f, resizable_none.x);
  EXPECT(0.0f, resizable_none.y);  // Variant 0 * 24

  Point non_resizable_none = WindowButtonTextureOffset(
      /*is_resizable=*/false, std::nullopt);
  EXPECT(18.0f, non_resizable_none.x);
  EXPECT(96.0f, non_resizable_none.y);  // Variant 4 * 24 = 96

  // Close button selected
  Point resizable_close = WindowButtonTextureOffset(
      /*is_resizable=*/true, WindowButton::Close);
  EXPECT(0.0f, resizable_close.x);
  EXPECT(72.0f, resizable_close.y);  // Variant 3 * 24 = 72

  Point non_resizable_close = WindowButtonTextureOffset(
      /*is_resizable=*/false, WindowButton::Close);
  EXPECT(18.0f, non_resizable_close.x);
  EXPECT(144.0f, non_resizable_close.y);  // Variant 6 * 24 = 144

  // Debug button selected
  Point resizable_debug = WindowButtonTextureOffset(
      /*is_resizable=*/true, WindowButton::Debug);
  EXPECT(0.0f, resizable_debug.x);
  EXPECT(48.0f, resizable_debug.y);  // Variant 2 * 24 = 48

  Point non_resizable_debug = WindowButtonTextureOffset(
      /*is_resizable=*/false, WindowButton::Debug);
  EXPECT(18.0f, non_resizable_debug.x);
  EXPECT(120.0f, non_resizable_debug.y);  // Variant 5 * 24 = 120

  // Toggle FullScreen selected
  Point resizable_fullscreen = WindowButtonTextureOffset(
      /*is_resizable=*/true, WindowButton::ToggleFullScreen);
  EXPECT(0.0f, resizable_fullscreen.x);
  EXPECT(24.0f, resizable_fullscreen.y);  // Variant 1 * 24 = 24
}

TEST(GetWindowButtonAtPointHitTesting) {
  // Resizable window button thresholds:
  // kFirstButtonThreshold = 21, kSecondButtonThreshold = 39
  // x < 21 -> ToggleFullScreen, 21 <= x < 39 -> Debug, x >= 39 -> Close

  EXPECT(WindowButton::ToggleFullScreen, GetWindowButtonAtPoint(10, true));
  EXPECT(WindowButton::Debug, GetWindowButtonAtPoint(25, true));
  EXPECT(WindowButton::Close, GetWindowButtonAtPoint(45, true));

  // Non-resizable window offsets unscaled_x by +18 (kButtonSize):
  // x = 0 -> unscaled_x = 18 < 21 -> ToggleFullScreen
  // x = 5 -> unscaled_x = 23 >= 21 -> Debug
  // x = 25 -> unscaled_x = 43 >= 39 -> Close
  EXPECT(WindowButton::ToggleFullScreen, GetWindowButtonAtPoint(0, false));
  EXPECT(WindowButton::Debug, GetWindowButtonAtPoint(5, false));
  EXPECT(WindowButton::Close, GetWindowButtonAtPoint(25, false));
}

}  // namespace
