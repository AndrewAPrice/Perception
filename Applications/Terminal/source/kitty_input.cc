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

#include "kitty_input.h"

#include <algorithm>

using ::perception::ui::KeyCode;
using ::perception::ui::ScancodeToAscii;
using ::perception::window::Cursor;
using ::perception::window::MouseButton;

namespace {

// Maximum depth of the Kitty keyboard protocol flag stack per screen.
constexpr size_t kMaxKeyboardStackDepth = 16;

// Maximum depth of the OSC 22 pointer shape stack.
constexpr size_t kMaxPointerShapeStackDepth = 16;

// Mask of all valid Kitty keyboard progressive enhancement flags (bits 0..4).
constexpr uint32 kValidKittyKeyFlagsMask = 0x1Fu;

// Kitty functional key code for Left Shift.
constexpr uint32 kKittyKeyLeftShift = 57441;

// Kitty functional key code for Left Control.
constexpr uint32 kKittyKeyLeftControl = 57442;

// Kitty functional key code for Left Alt.
constexpr uint32 kKittyKeyLeftAlt = 57443;

// Kitty functional key code for Left Super / Command.
constexpr uint32 kKittyKeyLeftSuper = 57444;

// Kitty functional key code for Right Shift.
constexpr uint32 kKittyKeyRightShift = 57447;

// Kitty functional key code for Right Super / Command.
constexpr uint32 kKittyKeyRightSuper = 57450;

// Maps a CSS / Kitty pointer shape name to a Perception Window Cursor enum.
Cursor MapPointerShapeName(std::string_view name, bool& is_override) {
  if (name.empty()) {
    is_override = false;
    return Cursor::Caret;
  }
  std::string lower;
  lower.reserve(name.size());
  for (char c : name) {
    if (c >= 'A' && c <= 'Z')
      lower.push_back(static_cast<char>(c - 'A' + 'a'));
    else
      lower.push_back(c);
  }
  is_override = true;
  if (lower == "text" || lower == "xterm" || lower == "ibeam" ||
      lower == "vertical-text" || lower == "caret") {
    return Cursor::Caret;
  }
  if (lower == "poke" || lower == "pointer" || lower == "hand" ||
      lower == "pointing_hand" || lower == "openhand") {
    return Cursor::Poke;
  }
  if (lower == "move" || lower == "all-scroll" || lower == "fleur" ||
      lower == "drag") {
    return Cursor::Drag;
  }
  if (lower == "grab" || lower == "grabbing" || lower == "closedhand")
    return Cursor::Grab;
  if (lower == "ew-resize" || lower == "e-resize" || lower == "w-resize" ||
      lower == "col-resize") {
    return Cursor::ResizeHorizontal;
  }
  if (lower == "ns-resize" || lower == "n-resize" || lower == "s-resize" ||
      lower == "row-resize") {
    return Cursor::ResizeVertical;
  }
  if (lower == "nwse-resize" || lower == "nw-resize" || lower == "se-resize")
    return Cursor::ResizeDiagonalTopLeftBottomRight;
  if (lower == "nesw-resize" || lower == "ne-resize" || lower == "sw-resize")
    return Cursor::ResizeDiagonalTopRightBottomLeft;
  if (lower == "pen")
    return Cursor::Pen;
  if (lower == "eraser")
    return Cursor::Eraser;
  if (lower == "none" || lower == "hidden")
    return Cursor::Hidden;
  return Cursor::Pointer;
}

// Formats a CSI sequence with optional modifier and event type parameters.
std::string FormatCsiFunctional(int number, char final_char, int mods,
                                int event_type, bool report_events) {
  std::string out = "\x1b[";
  bool need_params = (mods > 1) || (report_events && event_type != 1);
  if (number > 0 || need_params)
    out += std::to_string(number > 0 ? number : 1);
  if (need_params) {
    out += ';';
    out += std::to_string(mods);
    if (report_events && event_type != 1) {
      out += ':';
      out += std::to_string(event_type);
    }
  }
  out.push_back(final_char);
  return out;
}

}  // namespace

KittyInput::KittyInput()
    : primary_keyboard_stack_{0},
      alt_keyboard_stack_{0},
      current_pointer_shape_(Cursor::Caret),
      has_pointer_override_(false) {}

void KittyInput::Reset() {
  primary_keyboard_stack_.assign(1, 0);
  alt_keyboard_stack_.assign(1, 0);
  pointer_shape_stack_.clear();
  current_pointer_shape_ = Cursor::Caret;
  has_pointer_override_ = false;
}

void KittyInput::PushKeyboardFlags(uint32 flags, bool is_alt_screen) {
  auto& stack = StackForScreen(is_alt_screen);
  if (stack.size() >= kMaxKeyboardStackDepth)
    stack.erase(stack.begin());
  stack.push_back(flags & kValidKittyKeyFlagsMask);
}

void KittyInput::PopKeyboardFlags(int count, bool is_alt_screen) {
  auto& stack = StackForScreen(is_alt_screen);
  count = std::max(1, count);
  for (int i = 0; i < count && !stack.empty(); ++i)
    stack.pop_back();
  if (stack.empty())
    stack.push_back(0);
}

void KittyInput::SetKeyboardFlags(uint32 flags, int mode, bool is_alt_screen) {
  auto& stack = StackForScreen(is_alt_screen);
  if (stack.empty())
    stack.push_back(0);
  uint32 masked = flags & kValidKittyKeyFlagsMask;
  if (mode == 2) {
    stack.back() |= masked;
  } else if (mode == 3) {
    stack.back() &= ~masked;
  } else {
    stack.back() = masked;
  }
}

uint32 KittyInput::GetKeyboardFlags(bool is_alt_screen) const {
  const auto& stack = StackForScreen(is_alt_screen);
  return stack.empty() ? 0 : stack.back();
}

std::string KittyInput::FormatKeyboardQueryResponse(bool is_alt_screen) const {
  return "\x1b[?" + std::to_string(GetKeyboardFlags(is_alt_screen)) + "u";
}

std::string KittyInput::EncodeKeyEvent(uint8 scancode, bool shift, bool alt,
                                       bool ctrl, bool super, int event_type,
                                       bool is_alt_screen,
                                       bool application_cursor_keys) const {
  uint32 flags = GetKeyboardFlags(is_alt_screen);
  bool report_events = (flags & kKittyKeyReportEventTypes) != 0;
  if (event_type == 3 && !report_events)
    return "";

  int mods = 1 + (shift ? 1 : 0) + (alt ? 2 : 0) + (ctrl ? 4 : 0) +
             (super ? 8 : 0);
  KeyCode key = static_cast<KeyCode>(scancode);

  // Check modifier keys when kKittyKeyReportAllAsEscapeCodes is enabled.
  uint32 mod_key_code = 0;
  switch (key) {
    case KeyCode::LeftShift:
      mod_key_code = kKittyKeyLeftShift;
      break;
    case KeyCode::RightShift:
      mod_key_code = kKittyKeyRightShift;
      break;
    case KeyCode::LeftControl:
      mod_key_code = kKittyKeyLeftControl;
      break;
    case KeyCode::LeftAlt:
      mod_key_code = kKittyKeyLeftAlt;
      break;
    case KeyCode::LeftCommand:
      mod_key_code = kKittyKeyLeftSuper;
      break;
    case KeyCode::RightCommand:
      mod_key_code = kKittyKeyRightSuper;
      break;
    default:
      break;
  }
  if (mod_key_code != 0) {
    if ((flags & kKittyKeyReportAllAsEscapeCodes) == 0)
      return "";
    return FormatCsiFunctional(static_cast<int>(mod_key_code), 'u', mods,
                               event_type, report_events);
  }

  // Handle functional navigation and F-keys.
  switch (key) {
    case KeyCode::UpArrow:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOA";
      return FormatCsiFunctional(0, 'A', mods, event_type, report_events);
    case KeyCode::DownArrow:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOB";
      return FormatCsiFunctional(0, 'B', mods, event_type, report_events);
    case KeyCode::RightArrow:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOC";
      return FormatCsiFunctional(0, 'C', mods, event_type, report_events);
    case KeyCode::LeftArrow:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOD";
      return FormatCsiFunctional(0, 'D', mods, event_type, report_events);
    case KeyCode::Home:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOH";
      return FormatCsiFunctional(0, 'H', mods, event_type, report_events);
    case KeyCode::End:
      if (flags == 0 && mods == 1 && application_cursor_keys)
        return "\x1bOF";
      return FormatCsiFunctional(0, 'F', mods, event_type, report_events);
    case KeyCode::Insert:
      return FormatCsiFunctional(2, '~', mods, event_type, report_events);
    case KeyCode::Delete:
      return FormatCsiFunctional(3, '~', mods, event_type, report_events);
    case KeyCode::PageUp:
      return FormatCsiFunctional(5, '~', mods, event_type, report_events);
    case KeyCode::PageDown:
      return FormatCsiFunctional(6, '~', mods, event_type, report_events);
    case KeyCode::F1:
      if (flags == 0 && mods == 1)
        return "\x1bOP";
      return FormatCsiFunctional(0, 'P', mods, event_type, report_events);
    case KeyCode::F2:
      if (flags == 0 && mods == 1)
        return "\x1bOQ";
      return FormatCsiFunctional(0, 'Q', mods, event_type, report_events);
    case KeyCode::F3:
      if (flags == 0 && mods == 1)
        return "\x1bOR";
      return FormatCsiFunctional(0, 'R', mods, event_type, report_events);
    case KeyCode::F4:
      if (flags == 0 && mods == 1)
        return "\x1bOS";
      return FormatCsiFunctional(0, 'S', mods, event_type, report_events);
    case KeyCode::F5:
      return FormatCsiFunctional(15, '~', mods, event_type, report_events);
    case KeyCode::F6:
      return FormatCsiFunctional(17, '~', mods, event_type, report_events);
    case KeyCode::F7:
      return FormatCsiFunctional(18, '~', mods, event_type, report_events);
    case KeyCode::F8:
      return FormatCsiFunctional(19, '~', mods, event_type, report_events);
    case KeyCode::F9:
      return FormatCsiFunctional(20, '~', mods, event_type, report_events);
    case KeyCode::F10:
      return FormatCsiFunctional(21, '~', mods, event_type, report_events);
    case KeyCode::F11:
      return FormatCsiFunctional(23, '~', mods, event_type, report_events);
    case KeyCode::F12:
      return FormatCsiFunctional(24, '~', mods, event_type, report_events);
    case KeyCode::Escape:
      if ((flags & (kKittyKeyDisambiguateEscape |
                    kKittyKeyReportAllAsEscapeCodes)) ||
          mods > 1 || event_type == 3) {
        return FormatCsiFunctional(27, 'u', mods, event_type, report_events);
      }
      return "\x1b";
    case KeyCode::Enter:
      if ((flags & kKittyKeyReportAllAsEscapeCodes) ||
          ((flags & kKittyKeyDisambiguateEscape) && mods > 1) ||
          event_type == 3) {
        return FormatCsiFunctional(13, 'u', mods, event_type, report_events);
      }
      return alt ? "\x1b\r" : "\r";
    case KeyCode::Tab:
      if ((flags & kKittyKeyReportAllAsEscapeCodes) ||
          ((flags & kKittyKeyDisambiguateEscape) && mods > 1) ||
          event_type == 3) {
        return FormatCsiFunctional(9, 'u', mods, event_type, report_events);
      }
      return shift ? "\x1b[Z" : "\t";
    case KeyCode::Backspace:
      if ((flags & kKittyKeyReportAllAsEscapeCodes) ||
          ((flags & kKittyKeyDisambiguateEscape) && mods > 1) ||
          event_type == 3) {
        return FormatCsiFunctional(127, 'u', mods, event_type, report_events);
      }
      return alt ? "\x1b\x7f" : "\x7f";
    default:
      break;
  }

  char base_ch = ScancodeToAscii(scancode, false);
  char shifted_ch = ScancodeToAscii(scancode, shift);
  if (base_ch == '\0')
    return "";

  uint32 base_cp = static_cast<uint8>(base_ch);
  uint32 shifted_cp = static_cast<uint8>(shifted_ch);

  bool use_csi_u =
      (flags & kKittyKeyReportAllAsEscapeCodes) != 0 || event_type == 3 ||
      ((flags & kKittyKeyDisambiguateEscape) != 0 && (ctrl || alt || super));

  if (use_csi_u) {
    std::string out = "\x1b[";
    out += std::to_string(base_cp);
    if ((flags & kKittyKeyReportAlternateKeys) && shift &&
        shifted_cp != base_cp) {
      out += ':';
      out += std::to_string(shifted_cp);
    }
    bool need_second_field =
        (mods > 1) || (report_events && event_type != 1) ||
        ((flags & kKittyKeyReportAssociatedText) && event_type != 3 &&
         !ctrl && !alt && !super);
    if (need_second_field) {
      out += ';';
      out += std::to_string(mods);
      if (report_events && event_type != 1) {
        out += ':';
        out += std::to_string(event_type);
      }
    }
    if ((flags & kKittyKeyReportAssociatedText) && event_type != 3 && !ctrl &&
        !alt && !super && shifted_cp >= 32) {
      out += ';';
      out += std::to_string(shifted_cp);
    }
    out.push_back('u');
    return out;
  }

  if (ctrl) {
    char ctrl_byte = 0;
    if (base_ch >= 'a' && base_ch <= 'z') {
      ctrl_byte = static_cast<char>(base_ch - 'a' + 1);
    } else if (base_ch == '[' || base_ch == '2') {
      ctrl_byte = '\x00';
    } else if (base_ch == '\\') {
      ctrl_byte = '\x1c';
    } else if (base_ch == ']') {
      ctrl_byte = '\x1d';
    } else if (base_ch == '/' || base_ch == '-') {
      ctrl_byte = '\x1f';
    }
    if (ctrl_byte != 0 || base_ch == '2') {
      std::string out;
      if (alt)
        out.push_back('\x1b');
      out.push_back(ctrl_byte);
      return out;
    }
  }

  std::string out;
  if (alt)
    out.push_back('\x1b');
  out.push_back(shifted_ch);
  return out;
}

std::string KittyInput::EncodeMouseEvent(MouseButton button, bool is_release,
                                         bool is_motion, int wheel_delta,
                                         int col, int row, int pixel_x,
                                         int pixel_y, bool shift, bool alt,
                                         bool ctrl,
                                         int mouse_encoding_mode) const {
  int cb = 0;
  if (wheel_delta != 0) {
    cb = (wheel_delta > 0) ? 64 : 65;
  } else {
    switch (button) {
      case MouseButton::Left:
        cb = 0;
        break;
      case MouseButton::Middle:
        cb = 1;
        break;
      case MouseButton::Right:
        cb = 2;
        break;
      default:
        cb = 3;
        break;
    }
    if (is_release && mouse_encoding_mode == 0)
      cb = 3;
  }
  if (is_motion)
    cb += 32;
  if (shift)
    cb += 4;
  if (alt)
    cb += 8;
  if (ctrl)
    cb += 16;

  if (mouse_encoding_mode == 1016) {
    std::string out = "\x1b[<";
    out += std::to_string(cb);
    out += ';';
    out += std::to_string(std::max(0, pixel_x));
    out += ';';
    out += std::to_string(std::max(0, pixel_y));
    out.push_back(is_release ? 'm' : 'M');
    return out;
  }

  if (mouse_encoding_mode == 1006) {
    std::string out = "\x1b[<";
    out += std::to_string(cb);
    out += ';';
    out += std::to_string(std::max(1, col + 1));
    out += ';';
    out += std::to_string(std::max(1, row + 1));
    out.push_back(is_release ? 'm' : 'M');
    return out;
  }

  int clamped_col = std::clamp(col + 1, 1, 223);
  int clamped_row = std::clamp(row + 1, 1, 223);
  std::string out = "\x1b[M";
  out.push_back(static_cast<char>(32 + cb));
  out.push_back(static_cast<char>(32 + clamped_col));
  out.push_back(static_cast<char>(32 + clamped_row));
  return out;
}

void KittyInput::HandleOsc22(std::string_view payload) {
  if (payload.empty()) {
    current_pointer_shape_ = Cursor::Caret;
    has_pointer_override_ = false;
    return;
  }

  if (payload[0] == '<') {
    if (!pointer_shape_stack_.empty()) {
      current_pointer_shape_ = pointer_shape_stack_.back();
      pointer_shape_stack_.pop_back();
      has_pointer_override_ = !pointer_shape_stack_.empty();
    } else {
      current_pointer_shape_ = Cursor::Caret;
      has_pointer_override_ = false;
    }
    return;
  }

  bool push_stack = false;
  if (payload[0] == '>') {
    push_stack = true;
    payload.remove_prefix(1);
  }

  size_t comma = payload.find(',');
  std::string_view first_name =
      (comma == std::string_view::npos) ? payload : payload.substr(0, comma);

  if (push_stack) {
    if (pointer_shape_stack_.size() >= kMaxPointerShapeStackDepth)
      pointer_shape_stack_.erase(pointer_shape_stack_.begin());
    pointer_shape_stack_.push_back(current_pointer_shape_);
  }

  bool is_override = false;
  current_pointer_shape_ = MapPointerShapeName(first_name, is_override);
  has_pointer_override_ = is_override;
}
