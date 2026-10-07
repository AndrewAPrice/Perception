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

#include "output/blue_screen.h"

#include "output/blue_screen_font.h"
#include "output/framebuffer.h"
#include "scheduling/timer.h"
#include "types.h"

namespace output {

namespace {

// Duration in microseconds (10 seconds) to display the Blue Screen before
// powering off the system.
constexpr size_t kBlueScreenShutdownDelayMicroseconds = 10000000;

// Blue channel intensity for the Blue Screen background.
constexpr uint8 kBlueBackgroundIntensity = 0xAA;

// White channel intensity for foreground text.
constexpr uint8 kWhiteForegroundIntensity = 0xFF;

// 32-bit BGRX pixel value for the solid blue background.
constexpr uint32 kBluePixel32 = 0xFF0000AA;

// 32-bit BGRX pixel value for white foreground text.
constexpr uint32 kWhitePixel32 = 0xFFFFFFFF;

// 16-bit (5:6:5) pixel value for the solid blue background.
constexpr uint16 kBluePixel16 = 0x0015;

// 16-bit (5:6:5) pixel value for white foreground text.
constexpr uint16 kWhitePixel16 = 0xFFFF;

// 15-bit (5:5:5) pixel value for the solid blue background.
constexpr uint16 kBluePixel15 = 0x0015;

// 15-bit (5:5:5) pixel value for white foreground text.
constexpr uint16 kWhitePixel15 = 0x7FFF;

// Bit mask for the leftmost pixel in a font glyph row byte.
constexpr uint8 kLeftmostGlyphBit = 0x80;

// Whether the Blue Screen of Death is currently active.
bool g_blue_screen_enabled = false;

// Current horizontal pixel position for the next character.
uint32 g_cursor_x = 0;

// Current vertical pixel position for the next character.
uint32 g_cursor_y = 0;

// Returns the number of bytes per pixel for a given bits-per-pixel value.
uint32 BytesPerPixel(uint8 bits_per_pixel) {
  switch (bits_per_pixel) {
    case 15:
    case 16:
      return 2;
    case 24:
      return 3;
    case 32:
      return 4;
    default:
      return 0;
  }
}

// Writes a single pixel at (x, y) in either solid white or solid blue.
void WritePixel(const FramebufferDetails& fb, uint32 x, uint32 y,
                bool is_white) {
  uint32 bytes_per_pixel = BytesPerPixel(fb.bits_per_pixel);
  if (bytes_per_pixel == 0) return;

  uint8* pixel = fb.buffer + static_cast<size_t>(y) * fb.pitch +
                 static_cast<size_t>(x) * bytes_per_pixel;
  switch (fb.bits_per_pixel) {
    case 32:
      *reinterpret_cast<uint32*>(pixel) =
          is_white ? kWhitePixel32 : kBluePixel32;
      break;
    case 24:
      if (is_white) {
        pixel[0] = kWhiteForegroundIntensity;
        pixel[1] = kWhiteForegroundIntensity;
        pixel[2] = kWhiteForegroundIntensity;
      } else {
        pixel[0] = kBlueBackgroundIntensity;
        pixel[1] = 0;
        pixel[2] = 0;
      }
      break;
    case 16:
      *reinterpret_cast<uint16*>(pixel) =
          is_white ? kWhitePixel16 : kBluePixel16;
      break;
    case 15:
      *reinterpret_cast<uint16*>(pixel) =
          is_white ? kWhitePixel15 : kBluePixel15;
      break;
  }
}

// Fills the inclusive vertical row range [start_y, end_y) with solid blue.
void FillRowsBlue(const FramebufferDetails& fb, uint32 start_y, uint32 end_y) {
  if (fb.buffer == nullptr) return;
  if (end_y > fb.height) end_y = fb.height;
  for (uint32 y = start_y; y < end_y; y++) {
    for (uint32 x = 0; x < fb.width; x++) WritePixel(fb, x, y, false);
  }
}

// Copies all pixel rows up by one character line and colors the bottom line
// blue.
void ScrollUpOneLine(const FramebufferDetails& fb) {
  uint32 bytes_per_pixel = BytesPerPixel(fb.bits_per_pixel);
  if (bytes_per_pixel == 0 || g_cursor_y < kBlueScreenFontHeight) return;

  uint32 copy_height = g_cursor_y - kBlueScreenFontHeight;
  uint32 row_bytes = fb.width * bytes_per_pixel;

  for (uint32 y = 0; y < copy_height; y++) {
    uint8* dst = fb.buffer + static_cast<size_t>(y) * fb.pitch;
    const uint8* src =
        fb.buffer + static_cast<size_t>(y + kBlueScreenFontHeight) * fb.pitch;
    for (uint32 b = 0; b < row_bytes; b++) dst[b] = src[b];
  }

  FillRowsBlue(fb, copy_height, fb.height);
  g_cursor_y = copy_height;
}

// Moves the cursor to the beginning of the next line, scrolling if needed.
void AdvanceToNextLine(const FramebufferDetails& fb) {
  g_cursor_x = 0;
  g_cursor_y += kBlueScreenFontHeight;
  if (g_cursor_y + kBlueScreenFontHeight > fb.height) ScrollUpOneLine(fb);
}

}  // namespace

void EnableBlueScreen() {
  if (g_blue_screen_enabled) return;
  g_blue_screen_enabled = true;
  g_cursor_x = 0;
  g_cursor_y = 0;

  ReclaimFramebufferForBlueScreen();

  FramebufferDetails fb = GetFramebufferDetails();
  if (fb.buffer != nullptr && fb.width > 0 && fb.height > 0)
    FillRowsBlue(fb, 0, fb.height);

#ifndef TEST
  scheduling::ScheduleShutdownAfterMicroseconds(
      kBlueScreenShutdownDelayMicroseconds);
#endif
}

void PrintBlueScreenCharacter(char c) {
  if (!g_blue_screen_enabled) return;
  if (c == '\r') return;

  ReclaimFramebufferForBlueScreen();

  FramebufferDetails fb = GetFramebufferDetails();
  if (fb.buffer == nullptr || fb.width < kBlueScreenFontWidth ||
      fb.height < kBlueScreenFontHeight)
    return;

  if (c == '\n') {
    AdvanceToNextLine(fb);
    return;
  }

  if (g_cursor_x + kBlueScreenFontWidth > fb.width) AdvanceToNextLine(fb);

  const uint8* glyph = GetBlueScreenFontGlyph(c);
  for (uint32 gy = 0; gy < kBlueScreenFontHeight; gy++) {
    uint8 row_bits = glyph[gy];
    for (uint32 gx = 0; gx < kBlueScreenFontWidth; gx++) {
      if ((row_bits & (kLeftmostGlyphBit >> gx)) != 0)
        WritePixel(fb, g_cursor_x + gx, g_cursor_y + gy, true);
    }
  }

  g_cursor_x += kBlueScreenFontWidth;
}

void DisableBlueScreenForTest() {
  g_blue_screen_enabled = false;
  g_cursor_x = 0;
  g_cursor_y = 0;
}

}  // namespace output
