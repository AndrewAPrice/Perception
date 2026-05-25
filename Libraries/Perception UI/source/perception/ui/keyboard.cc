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

#include "perception/ui/keyboard.h"

namespace perception {
namespace ui {

char ScancodeToAscii(uint8 scancode, bool shift) {
  switch (scancode) {
    // Numbers row
    case 0x02:
      return shift ? '!' : '1';
    case 0x03:
      return shift ? '@' : '2';
    case 0x04:
      return shift ? '#' : '3';
    case 0x05:
      return shift ? '$' : '4';
    case 0x06:
      return shift ? '%' : '5';
    case 0x07:
      return shift ? '^' : '6';
    case 0x08:
      return shift ? '&' : '7';
    case 0x09:
      return shift ? '*' : '8';
    case 0x0A:
      return shift ? '(' : '9';
    case 0x0B:
      return shift ? ')' : '0';
    case 0x0C:
      return shift ? '_' : '-';
    case 0x0D:
      return shift ? '+' : '=';

    // Letters row 1
    case 0x10:
      return shift ? 'Q' : 'q';
    case 0x11:
      return shift ? 'W' : 'w';
    case 0x12:
      return shift ? 'E' : 'e';
    case 0x13:
      return shift ? 'R' : 'r';
    case 0x14:
      return shift ? 'T' : 't';
    case 0x15:
      return shift ? 'Y' : 'y';
    case 0x16:
      return shift ? 'U' : 'u';
    case 0x17:
      return shift ? 'I' : 'i';
    case 0x18:
      return shift ? 'O' : 'o';
    case 0x19:
      return shift ? 'P' : 'p';
    case 0x1A:
      return shift ? '{' : '[';
    case 0x1B:
      return shift ? '}' : ']';

    // Letters row 2
    case 0x1E:
      return shift ? 'A' : 'a';
    case 0x1F:
      return shift ? 'S' : 's';
    case 0x20:
      return shift ? 'D' : 'd';
    case 0x21:
      return shift ? 'F' : 'f';
    case 0x22:
      return shift ? 'G' : 'g';
    case 0x23:
      return shift ? 'H' : 'h';
    case 0x24:
      return shift ? 'J' : 'j';
    case 0x25:
      return shift ? 'K' : 'k';
    case 0x26:
      return shift ? 'L' : 'l';
    case 0x27:
      return shift ? ':' : ';';
    case 0x28:
      return shift ? '"' : '\'';
    case 0x2B:
      return shift ? '|' : '\\';

    // Letters row 3
    case 0x2C:
      return shift ? 'Z' : 'z';
    case 0x2D:
      return shift ? 'X' : 'x';
    case 0x2E:
      return shift ? 'C' : 'c';
    case 0x2F:
      return shift ? 'V' : 'v';
    case 0x30:
      return shift ? 'B' : 'b';
    case 0x31:
      return shift ? 'N' : 'n';
    case 0x32:
      return shift ? 'M' : 'm';
    case 0x33:
      return shift ? '<' : ',';
    case 0x34:
      return shift ? '>' : '.';
    case 0x35:
      return shift ? '?' : '/';

    // Numpad / other symbols
    case 0x39:
      return ' ';  // Space

    default:
      return '\0';
  }
}

}  // namespace ui
}  // namespace perception
