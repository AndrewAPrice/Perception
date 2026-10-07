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

namespace output {

// Width of a single Blue Screen font glyph in pixels.
constexpr uint32 kBlueScreenFontWidth = 8;

// Height of a single Blue Screen font glyph in pixels.
constexpr uint32 kBlueScreenFontHeight = 16;

// Returns a pointer to a kBlueScreenFontHeight-byte bitmap for the given ASCII
// character. Each byte represents one pixel row from top to bottom, with bit 7
// corresponding to the leftmost pixel and bit 0 to the rightmost pixel.
const uint8* GetBlueScreenFontGlyph(char c);

}  // namespace output
