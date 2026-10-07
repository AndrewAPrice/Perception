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
#include "testing.h"

namespace output {

namespace {

// Width of the test framebuffer (2 character columns).
constexpr uint32 kTestWidth = kBlueScreenFontWidth * 2;

// Height of the test framebuffer (2 character rows).
constexpr uint32 kTestHeight = kBlueScreenFontHeight * 2;

// Bits per pixel of the test framebuffer.
constexpr uint8 kTestBpp = 32;

// Bytes per pixel of the test framebuffer.
constexpr uint32 kTestBytesPerPixel = 4;

// Pitch in bytes per row of the test framebuffer.
constexpr uint32 kTestPitch = kTestWidth * kTestBytesPerPixel;

// Expected 32-bit pixel value for solid blue background.
constexpr uint32 kExpectedBluePixel = 0xFF0000AA;

// Expected 32-bit pixel value for white foreground text.
constexpr uint32 kExpectedWhitePixel = 0xFFFFFFFF;

// Helper to count white pixels in a character cell (cell_x, cell_y).
uint32 CountWhitePixelsInCell(const uint32* pixels, uint32 cell_x,
                              uint32 cell_y) {
  uint32 count = 0;
  uint32 start_x = cell_x * kBlueScreenFontWidth;
  uint32 start_y = cell_y * kBlueScreenFontHeight;
  for (uint32 y = 0; y < kBlueScreenFontHeight; y++) {
    for (uint32 x = 0; x < kBlueScreenFontWidth; x++) {
      if (pixels[(start_y + y) * kTestWidth + (start_x + x)] ==
          kExpectedWhitePixel)
        count++;
    }
  }
  return count;
}

}  // namespace

TEST(BlueScreenNoOpWhenDisabled) {
  uint32 buffer[kTestWidth * kTestHeight];
  for (uint32 i = 0; i < kTestWidth * kTestHeight; i++) buffer[i] = 0;

  DisableBlueScreenForTest();
  SetFramebufferDetailsForTest(reinterpret_cast<uint8*>(buffer), kTestWidth,
                               kTestHeight, kTestPitch, kTestBpp);

  PrintBlueScreenCharacter('A');
  ASSERT(buffer[0], static_cast<uint32>(0));
  ASSERT(CountWhitePixelsInCell(buffer, 0, 0), static_cast<uint32>(0));
}

TEST(EnableBlueScreenFillsScreenBlue) {
  uint32 buffer[kTestWidth * kTestHeight];
  for (uint32 i = 0; i < kTestWidth * kTestHeight; i++) buffer[i] = 0;

  DisableBlueScreenForTest();
  SetFramebufferDetailsForTest(reinterpret_cast<uint8*>(buffer), kTestWidth,
                               kTestHeight, kTestPitch, kTestBpp);

  EnableBlueScreen();

  for (uint32 i = 0; i < kTestWidth * kTestHeight; i++)
    ASSERT(buffer[i], kExpectedBluePixel);

  DisableBlueScreenForTest();
}

TEST(BlueScreenWrapsAndScrollsDirectlyInFramebuffer) {
  uint32 buffer[kTestWidth * kTestHeight];
  for (uint32 i = 0; i < kTestWidth * kTestHeight; i++) buffer[i] = 0;

  DisableBlueScreenForTest();
  SetFramebufferDetailsForTest(reinterpret_cast<uint8*>(buffer), kTestWidth,
                               kTestHeight, kTestPitch, kTestBpp);

  EnableBlueScreen();

  // Print 'A' at (0, 0) and '\n' to advance to (0, 1).
  PrintBlueScreenCharacter('A');
  uint32 a_white_pixels = CountWhitePixelsInCell(buffer, 0, 0);
  ASSERT(a_white_pixels > 0, true);
  ASSERT(CountWhitePixelsInCell(buffer, 1, 0), static_cast<uint32>(0));

  PrintBlueScreenCharacter('\n');
  // Print 'B' and 'C' on the bottom line (row 1, cells 0 and 1).
  PrintBlueScreenCharacter('B');
  PrintBlueScreenCharacter('C');
  uint32 b_white_pixels = CountWhitePixelsInCell(buffer, 0, 1);
  uint32 c_white_pixels = CountWhitePixelsInCell(buffer, 1, 1);
  ASSERT(b_white_pixels > 0, true);
  ASSERT(c_white_pixels > 0, true);

  // Printing 'D' exceeds the width of row 1, scrolling row 1 ('B', 'C') up to
  // row 0, clearing row 1 to blue, and drawing 'D' at (0, 1).
  PrintBlueScreenCharacter('D');
  uint32 d_white_pixels = CountWhitePixelsInCell(buffer, 0, 1);

  ASSERT(CountWhitePixelsInCell(buffer, 0, 0), b_white_pixels);
  ASSERT(CountWhitePixelsInCell(buffer, 1, 0), c_white_pixels);
  ASSERT(d_white_pixels > 0, true);
  ASSERT(CountWhitePixelsInCell(buffer, 1, 1), static_cast<uint32>(0));

  DisableBlueScreenForTest();
}

}  // namespace output
