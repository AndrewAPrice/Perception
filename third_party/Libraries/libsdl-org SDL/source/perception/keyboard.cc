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

#include "keyboard.h"

SDL_Scancode SDLScanCodeForPerceptionKeyCode(unsigned char key) {
  switch (key) {
    case 0x01:
      return SDL_SCANCODE_ESCAPE;
    case 0x02:
      return SDL_SCANCODE_1;
    case 0x03:
      return SDL_SCANCODE_2;
    case 0x04:
      return SDL_SCANCODE_3;
    case 0x05:
      return SDL_SCANCODE_4;
    case 0x06:
      return SDL_SCANCODE_5;
    case 0x07:
      return SDL_SCANCODE_6;
    case 0x08:
      return SDL_SCANCODE_7;
    case 0x09:
      return SDL_SCANCODE_8;
    case 0x0A:
      return SDL_SCANCODE_9;
    case 0x0B:
      return SDL_SCANCODE_0;
    case 0x0C:
      return SDL_SCANCODE_MINUS;
    case 0x0D:
      return SDL_SCANCODE_EQUALS;
    case 0x0E:
      return SDL_SCANCODE_BACKSPACE;
    case 0x0F:
      return SDL_SCANCODE_TAB;
    case 0x10:
      return SDL_SCANCODE_Q;
    case 0x11:
      return SDL_SCANCODE_W;
    case 0x12:
      return SDL_SCANCODE_E;
    case 0x13:
      return SDL_SCANCODE_R;
    case 0x14:
      return SDL_SCANCODE_T;
    case 0x15:
      return SDL_SCANCODE_Y;
    case 0x16:
      return SDL_SCANCODE_U;
    case 0x17:
      return SDL_SCANCODE_I;
    case 0x18:
      return SDL_SCANCODE_O;
    case 0x19:
      return SDL_SCANCODE_P;
    case 0x1A:
      return SDL_SCANCODE_LEFTBRACKET;
    case 0x1B:
      return SDL_SCANCODE_RIGHTBRACKET;
    case 0x1C:
      return SDL_SCANCODE_RETURN;
    case 0x1D:
      return SDL_SCANCODE_LCTRL;
    case 0x1E:
      return SDL_SCANCODE_A;
    case 0x1F:
      return SDL_SCANCODE_S;
    case 0x20:
      return SDL_SCANCODE_D;
    case 0x21:
      return SDL_SCANCODE_F;
    case 0x22:
      return SDL_SCANCODE_G;
    case 0x23:
      return SDL_SCANCODE_H;
    case 0x24:
      return SDL_SCANCODE_J;
    case 0x25:
      return SDL_SCANCODE_K;
    case 0x26:
      return SDL_SCANCODE_L;
    case 0x27:
      return SDL_SCANCODE_SEMICOLON;
    case 0x28:
      return SDL_SCANCODE_APOSTROPHE;
    case 0x29:
      return SDL_SCANCODE_GRAVE;
    case 0x2A:
      return SDL_SCANCODE_LSHIFT;
    case 0x2B:
      return SDL_SCANCODE_BACKSLASH;
    case 0x2C:
      return SDL_SCANCODE_Z;
    case 0x2D:
      return SDL_SCANCODE_X;
    case 0x2E:
      return SDL_SCANCODE_C;
    case 0x2F:
      return SDL_SCANCODE_V;
    case 0x30:
      return SDL_SCANCODE_B;
    case 0x31:
      return SDL_SCANCODE_N;
    case 0x32:
      return SDL_SCANCODE_M;
    case 0x33:
      return SDL_SCANCODE_COMMA;
    case 0x34:
      return SDL_SCANCODE_PERIOD;
    case 0x35:
      return SDL_SCANCODE_SLASH;
    case 0x36:
      return SDL_SCANCODE_RSHIFT;
    case 0x38:
      return SDL_SCANCODE_LALT;
    case 0x39:
      return SDL_SCANCODE_SPACE;
    case 0x48:
      return SDL_SCANCODE_UP;
    case 0x50:
      return SDL_SCANCODE_DOWN;
    case 0x4B:
      return SDL_SCANCODE_LEFT;
    case 0x4D:
      return SDL_SCANCODE_RIGHT;
    default:
      return SDL_SCANCODE_UNKNOWN;
  }
}
