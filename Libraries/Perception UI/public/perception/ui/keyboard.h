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
  Escape = 0x01,
  One = 0x02,
  Two = 0x03,
  Three = 0x04,
  Four = 0x05,
  Five = 0x06,
  Six = 0x07,
  Seven = 0x08,
  Eight = 0x09,
  Nine = 0x0A,
  Zero = 0x0B,
  Hyphen = 0x0C,
  Equals = 0x0D,
  Backspace = 0x0E,
  Tab = 0x0F,
  Q = 0x10,
  W = 0x11,
  E = 0x12,
  R = 0x13,
  T = 0x14,
  Y = 0x15,
  U = 0x16,
  I = 0x17,
  O = 0x18,
  P = 0x19,
  LeftBracket = 0x1A,
  RightBracket = 0x1B,
  Enter = 0x1C,
  LeftControl = 0x1D,
  A = 0x1E,
  S = 0x1F,
  D = 0x20,
  F = 0x21,
  G = 0x22,
  H = 0x23,
  J = 0x24,
  K = 0x25,
  L = 0x26,
  Semicolon = 0x27,
  Apostrophe = 0x28,
  Backtick = 0x29,
  LeftShift = 0x2A,
  Backslash = 0x2B,
  Z = 0x2C,
  X = 0x2D,
  C = 0x2E,
  V = 0x2F,
  B = 0x30,
  N = 0x31,
  M = 0x32,
  Comma = 0x33,
  Period = 0x34,
  Slash = 0x35,
  RightShift = 0x36,
  KeypadAsterisk = 0x37,
  LeftAlt = 0x38,
  Space = 0x39,
  CapsLock = 0x3A,
  F1 = 0x3B,
  F2 = 0x3C,
  F3 = 0x3D,
  F4 = 0x3E,
  F5 = 0x3F,
  F6 = 0x40,
  F7 = 0x41,
  F8 = 0x42,
  F9 = 0x43,
  F10 = 0x44,
  NumLock = 0x45,
  ScrollLock = 0x46,
  Home = 0x47,
  UpArrow = 0x48,
  PageUp = 0x49,
  KeypadMinus = 0x4A,
  LeftArrow = 0x4B,
  Keypad5 = 0x4C,
  RightArrow = 0x4D,
  KeypadPlus = 0x4E,
  End = 0x4F,
  DownArrow = 0x50,
  PageDown = 0x51,
  Insert = 0x52,
  Delete = 0x53,
  F11 = 0x57,
  F12 = 0x58,
  LeftCommand = 0x5B,
  RightCommand = 0x5C,
  Menu = 0x5D
};

// Converts a keyboard scancode to its corresponding ASCII character.
// Returns '\0' if the scancode doesn't map to a printable ASCII character.
char ScancodeToAscii(uint8 scancode, bool shift_pressed);

// Determines whether  a scancode is a shift key.
inline bool IsShiftKey(uint8 scancode) {
  return scancode == static_cast<uint8>(KeyCode::LeftShift) ||
         scancode == static_cast<uint8>(KeyCode::RightShift);
}

// Determines whether a scancode is a control/command key.
inline bool IsControlKey(uint8 scancode) {
  return scancode == static_cast<uint8>(KeyCode::LeftControl) ||
         scancode == static_cast<uint8>(KeyCode::LeftCommand) ||
         scancode == static_cast<uint8>(KeyCode::RightCommand);
}

}  // namespace ui
}  // namespace perception
