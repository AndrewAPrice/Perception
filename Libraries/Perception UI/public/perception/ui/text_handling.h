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

#include "include/core/SkFont.h"

namespace perception {
namespace ui {

// Returns the byte length of the UTF-8 character starting at `index` in `s`.
// If `index >= s.length()`, returns 0.
size_t GetNextUtf8CharLength(std::string_view s, size_t index);

// Returns the starting byte index of the UTF-8 character immediately preceding
// `index` in `s`. If `index == 0` or `index > s.length()`, returns 0.
size_t GetPreviousUtf8CharIndex(std::string_view s, size_t index);

// Removes the last UTF-8 character from `s`.
void RemoveLastUtf8Character(std::string& s);

// Returns the byte index within `text` whose horizontal position when rendered
// with `font` is closest to `target_x`.
size_t FindClosestCursorIndex(std::string_view text, float target_x,
                              SkFont* font);

}  // namespace ui
}  // namespace perception
