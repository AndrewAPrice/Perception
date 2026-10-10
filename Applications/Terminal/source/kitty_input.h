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

#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/keyboard.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"
#include "types.h"

namespace {

// Kitty keyboard progressive enhancement flag: disambiguate escape codes.
constexpr uint32 kKittyKeyDisambiguateEscape = 1 << 0;

// Kitty keyboard progressive enhancement flag: report key press/repeat/release event types.
constexpr uint32 kKittyKeyReportEventTypes = 1 << 1;

// Kitty keyboard progressive enhancement flag: report alternate shifted/base layout keys.
constexpr uint32 kKittyKeyReportAlternateKeys = 1 << 2;

// Kitty keyboard progressive enhancement flag: report all keys as CSI u escape codes.
constexpr uint32 kKittyKeyReportAllAsEscapeCodes = 1 << 3;

// Kitty keyboard progressive enhancement flag: report associated text as codepoints.
constexpr uint32 kKittyKeyReportAssociatedText = 1 << 4;

}  // namespace

// Manages the Kitty Keyboard Protocol flag stacks, mouse event encoding
// (X10, SGR-Cells ?1006h, SGR-Pixels ?1016h), and Kitty Pointer Shapes (OSC 22).
class KittyInput {
 public:
  // Constructs a KittyInput manager with default stacks and pointer shape.
  KittyInput();

  // Resets keyboard flag stacks and pointer shape stack to defaults.
  void Reset();

  // Pushes a new flag bitmask onto the active screen's keyboard protocol stack (`CSI > flags u`).
  void PushKeyboardFlags(uint32 flags, bool is_alt_screen);

  // Pops `count` entries from the active screen's keyboard protocol stack (`CSI < count u`).
  void PopKeyboardFlags(int count, bool is_alt_screen);

  // Modifies the top of the active screen's keyboard protocol stack (`CSI = flags ; mode u`).
  void SetKeyboardFlags(uint32 flags, int mode, bool is_alt_screen);

  // Returns the active Kitty keyboard protocol flags for the specified screen.
  uint32 GetKeyboardFlags(bool is_alt_screen) const;

  // Formats the response to a `CSI ? u` query (`\x1b[?<flags>u`).
  std::string FormatKeyboardQueryResponse(bool is_alt_screen) const;

  // Encodes a keyboard key press/repeat/release event into an escape sequence or character
  // according to the active Kitty keyboard flags or legacy VT100/xterm rules.
  std::string EncodeKeyEvent(uint8 scancode, bool shift, bool alt, bool ctrl,
                             bool super, int event_type, bool is_alt_screen,
                             bool application_cursor_keys) const;

  // Encodes a mouse button or motion event according to the active mouse tracking
  // and coordinate encoding modes (`0`, `1006` SGR-Cells, or `1016` SGR-Pixels).
  std::string EncodeMouseEvent(::perception::window::MouseButton button,
                               bool is_release, bool is_motion, int wheel_delta,
                               int col, int row, int pixel_x, int pixel_y,
                               bool shift, bool alt, bool ctrl,
                               int mouse_encoding_mode) const;

  // Processes a Kitty Pointer Shape command (`OSC 22 ; payload ST`).
  void HandleOsc22(std::string_view payload);

  // Returns whether an explicit OSC 22 pointer shape override is currently active.
  bool HasPointerShapeOverride() const { return has_pointer_override_; }

  // Returns the current mouse cursor shape from the OSC 22 pointer stack.
  ::perception::window::Cursor GetPointerShape() const {
    return current_pointer_shape_;
  }

 private:
  // Returns a mutable reference to the flag stack for `is_alt_screen`.
  std::vector<uint32>& StackForScreen(bool is_alt_screen) {
    return is_alt_screen ? alt_keyboard_stack_ : primary_keyboard_stack_;
  }

  // Returns a const reference to the flag stack for `is_alt_screen`.
  const std::vector<uint32>& StackForScreen(bool is_alt_screen) const {
    return is_alt_screen ? alt_keyboard_stack_ : primary_keyboard_stack_;
  }

  std::vector<uint32> primary_keyboard_stack_;
  std::vector<uint32> alt_keyboard_stack_;

  std::vector<::perception::window::Cursor> pointer_shape_stack_;
  ::perception::window::Cursor current_pointer_shape_;
  bool has_pointer_override_;
};
