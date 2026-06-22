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

#include "layout.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>

extern "C" {
#include "utils/errors.h"
#include <libwapcaplet/libwapcaplet.h>

#include "netsurf/layout.h"
#include "netsurf/plot_style.h"
}

#include "include/core/SkFont.h"
#include "include/core/SkFontTypes.h"
#include "include/core/SkRect.h"
#include "perception/ui/font.h"

namespace netsurf {
namespace perception {

SkFont* GetSkiaFont(const struct plot_font_style* fstyle) {
  std::string family_name;
  if (fstyle->families) {
    for (int i = 0; fstyle->families[i] != nullptr; ++i) {
      std::string name(lwc_string_data(fstyle->families[i]),
                       lwc_string_length(fstyle->families[i]));
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (name.find("dejavu") != std::string::npos ||
          name.find("sans") != std::string::npos ||
          name.find("serif") != std::string::npos ||
          name.find("mono") != std::string::npos ||
          name.find("math") != std::string::npos ||
          name.find("courier") != std::string::npos ||
          name.find("times") != std::string::npos ||
          name.find("arial") != std::string::npos ||
          name.find("helvetica") != std::string::npos ||
          name.find("georgia") != std::string::npos ||
          name.find("tahoma") != std::string::npos ||
          name.find("verdana") != std::string::npos) {
        family_name = std::string(lwc_string_data(fstyle->families[i]),
                                  lwc_string_length(fstyle->families[i]));
        break;
      }
    }
  }
  if (family_name.empty()) {
    switch (fstyle->family) {
      case PLOT_FONT_FAMILY_SERIF:
        family_name = "DejaVuSerif";
        break;
      case PLOT_FONT_FAMILY_MONOSPACE:
        family_name = "DejaVuSansMono";
        break;
      case PLOT_FONT_FAMILY_SANS_SERIF:
      default:
        family_name = "DejaVuSans";
        break;
    }
  }

  float size = plot_style_fixed_to_float(fstyle->size);
  if (size <= 0.0f) size = 12.0f;

  int weight = fstyle->weight;
  if (weight <= 0) weight = 400;

  bool is_italic = (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) != 0;

  return ::perception::ui::GetUiFont(
      family_name, size, weight,
      is_italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant,
      SkFontStyle::kNormal_Width);
}

namespace {

size_t GetNextUtf8CharLength(std::string_view s, size_t index) {
  if (index >= s.length()) return 0;
  unsigned char c = s[index];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

nserror FontWidth(const struct plot_font_style* fstyle, const char* text,
                  size_t length, int* width) {
  if (!text || length == 0) {
    *width = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *width = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *width = 0;
    return NSERROR_OK;
  }
  SkRect bounds;
  font->measureText(text, safe_len, SkTextEncoding::kUTF8, &bounds);
  *width = (int)std::ceil(bounds.width());
  return NSERROR_OK;
}

nserror FontPosition(const struct plot_font_style* fstyle, const char* text,
                     size_t length, int x, size_t* char_offset, int* actual_x) {
  if (!text || length == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  float best_dist = std::abs((float)x);
  size_t best_idx = 0;
  float best_x = 0.0f;

  std::string_view sv(text, safe_len);
  size_t idx = 0;
  while (true) {
    SkRect bounds;
    font->measureText(text, idx, SkTextEncoding::kUTF8, &bounds);
    float w = bounds.width();
    float dist = std::abs(w - x);
    if (dist < best_dist) {
      best_dist = dist;
      best_idx = idx;
      best_x = w;
    } else {
      break;
    }
    if (idx >= safe_len) break;
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  *char_offset = best_idx;
  *actual_x = (int)best_x;
  return NSERROR_OK;
}

nserror FontSplit(const struct plot_font_style* fstyle, const char* text,
                  size_t length, int x, size_t* char_offset, int* actual_x) {
  if (!text || length == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t best_idx = 0;
  float best_x = 0.0f;

  std::string_view sv(text, safe_len);
  size_t idx = 0;
  while (true) {
    SkRect bounds;
    font->measureText(text, idx, SkTextEncoding::kUTF8, &bounds);
    float w = bounds.width();
    if (w <= x) {
      best_idx = idx;
      best_x = w;
    } else {
      break;
    }
    if (idx >= safe_len) break;
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  *char_offset = best_idx;
  *actual_x = (int)best_x;
  return NSERROR_OK;
}

}  // namespace

struct gui_layout_table skia_layout_table = {
    .width = FontWidth,
    .position = FontPosition,
    .split = FontSplit,
};

}  // namespace perception
}  // namespace netsurf
