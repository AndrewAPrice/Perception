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

namespace {

// Conversion factor from typographic points (1/72 inch) to pixels at standard
// 96 DPI.
constexpr float kPointsToPixels = 96.0f / 72.0f;

// Default fallback font size in points if unspecified or non-positive.
constexpr float kDefaultFontSizePt = 12.0f;

}  // namespace

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

  float pt_size = plot_style_fixed_to_float(fstyle->size);
  if (pt_size <= 0.0f) pt_size = kDefaultFontSizePt;
  float size = pt_size * kPointsToPixels;

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
  float advance = font->measureText(text, safe_len, SkTextEncoding::kUTF8);
  *width = (int)std::ceil(advance);
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
    float w = font->measureText(text, idx, SkTextEncoding::kUTF8);
    float dist = std::abs(w - (float)x);
    if (dist < best_dist) {
      best_dist = dist;
      best_idx = idx;
      best_x = w;
    }
    if (idx >= safe_len) break;
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  *char_offset = best_idx;
  *actual_x = (int)std::round(best_x);
  return NSERROR_OK;
}

nserror FontSplit(const struct plot_font_style* fstyle, const char* text,
                  size_t length, int x, size_t* char_offset, int* actual_x) {
  if (!text || length == 0) {
    *char_offset = 1;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *char_offset = 1;
    *actual_x = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *char_offset = safe_len;
    *actual_x = 0;
    return NSERROR_OK;
  }

  // Check if the entire string fits in the available width.
  float total_advance =
      font->measureText(text, safe_len, SkTextEncoding::kUTF8);
  if (total_advance <= (float)x) {
    *char_offset = safe_len;
    *actual_x = (int)std::ceil(total_advance);
    return NSERROR_OK;
  }

  std::string_view sv(text, safe_len);
  size_t last_space_fit = 0;
  float last_space_fit_w = 0.0f;
  size_t first_space = 0;
  float first_space_w = 0.0f;

  size_t last_char_fit = 0;
  float last_char_fit_w = 0.0f;

  size_t idx = 0;
  while (idx < safe_len) {
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    size_t next_idx = idx + char_len;

    float w = font->measureText(text, next_idx, SkTextEncoding::kUTF8);
    if (w <= (float)x) {
      last_char_fit = next_idx;
      last_char_fit_w = w;
    }

    if (text[idx] == ' ' && idx > 0) {
      float w_before_space =
          font->measureText(text, idx, SkTextEncoding::kUTF8);
      if (first_space == 0) {
        first_space = idx;
        first_space_w = w_before_space;
      }
      if (w_before_space <= (float)x) {
        last_space_fit = idx;
        last_space_fit_w = w_before_space;
      }
    }

    idx = next_idx;
  }

  if (last_space_fit > 0) {
    *char_offset = last_space_fit;
    *actual_x = (int)std::ceil(last_space_fit_w);
    return NSERROR_OK;
  }

  if (first_space > 0) {
    *char_offset = first_space;
    *actual_x = (int)std::ceil(first_space_w);
    return NSERROR_OK;
  }

  if (last_char_fit > 0) {
    *char_offset = last_char_fit;
    *actual_x = (int)std::ceil(last_char_fit_w);
    return NSERROR_OK;
  }

  // Ensure char_offset is never 0: take at least the first character.
  size_t first_char_len = GetNextUtf8CharLength(sv, 0);
  if (first_char_len == 0) first_char_len = 1;
  if (first_char_len > safe_len) first_char_len = safe_len;
  *char_offset = first_char_len;
  *actual_x = (int)std::ceil(
      font->measureText(text, first_char_len, SkTextEncoding::kUTF8));
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
