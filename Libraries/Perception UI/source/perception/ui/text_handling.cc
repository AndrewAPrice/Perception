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

#include "perception/ui/text_handling.h"

#include <cmath>

#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"

namespace perception {
namespace ui {

size_t GetNextUtf8CharLength(std::string_view s, size_t index) {
  if (index >= s.length()) return 0;
  unsigned char c = s[index];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

size_t GetPreviousUtf8CharIndex(std::string_view s, size_t index) {
  if (index == 0 || index > s.length()) return 0;
  size_t prev = index - 1;
  while (prev > 0 && (static_cast<unsigned char>(s[prev]) & 0xC0) == 0x80) {
    prev--;
  }
  return prev;
}

void RemoveLastUtf8Character(std::string& s) {
  if (s.empty()) return;
  s.pop_back();
  while (!s.empty() && (static_cast<unsigned char>(s.back()) & 0xC0) == 0x80) {
    s.pop_back();
  }
}

size_t FindClosestCursorIndex(std::string_view text, float target_x,
                              SkFont* font) {
  if (text.empty()) return 0;
  size_t best_offset = 0;
  float best_dist = std::abs(target_x);
  size_t idx = 0;
  while (idx <= text.length()) {
    float char_x = font->measureText(text.data(), idx, SkTextEncoding::kUTF8);
    float dist = std::abs(char_x - target_x);
    if (dist < best_dist) {
      best_dist = dist;
      best_offset = idx;
    } else if (dist > best_dist) {
      break;
    }
    if (idx == text.length()) break;
    size_t char_len = GetNextUtf8CharLength(text, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  return best_offset;
}

}  // namespace ui
}  // namespace perception
