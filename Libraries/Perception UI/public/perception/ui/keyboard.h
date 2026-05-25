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

#pragma once

#include "types.h"

namespace perception {
namespace ui {

// Enums for standard keyboard scancodes.
enum class KeyCode : uint8 {
  Backspace = 0x0E,
  Enter = 0x1C,
  LeftShift = 0x2A,
  RightShift = 0x36,
  Space = 0x39,
  Home = 0x47,
  LeftArrow = 0x4B,
  RightArrow = 0x4D,
  End = 0x4F,
  Delete = 0x53
};

// Converts a keyboard scancode to its corresponding ASCII character.
// Returns '\0' if the scancode doesn't map to a printable ASCII character.
char ScancodeToAscii(uint8 scancode, bool shift_pressed);

// Determines whether  a scancode is a shift key.
inline bool IsShiftKey(uint8 scancode) {
  return scancode == static_cast<uint8>(KeyCode::LeftShift) ||
         scancode == static_cast<uint8>(KeyCode::RightShift);
}

}  // namespace ui
}  // namespace perception
