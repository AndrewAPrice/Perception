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
#include <vector>

extern "C" {
#include "utils/errors.h"
#include <libwapcaplet/libwapcaplet.h>

#include "netsurf/browser.h"
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

// Default fallback font size in points if unspecified or non-positive.
constexpr float kDefaultFontSizePt = 12.0f;

// Scaling factor applied to font size for small-caps variant.
constexpr float kSmallCapsScale = 0.8f;

}  // namespace

SkFont* GetSkiaFont(const struct plot_font_style* fstyle) {
  std::string family_name;
  if (fstyle->families) {
    for (int i = 0; fstyle->families[i] != nullptr; ++i) {
      std::string name(lwc_string_data(fstyle->families[i]),
                       lwc_string_length(fstyle->families[i]));
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char ch) { return std::tolower(ch); });
      if (name.find("mono") != std::string::npos ||
          name.find("courier") != std::string::npos ||
          name.find("consolas") != std::string::npos ||
          name.find("menlo") != std::string::npos ||
          name.find("monaco") != std::string::npos ||
          name.find("inconsolata") != std::string::npos ||
          name.find("fira code") != std::string::npos ||
          name.find("source code") != std::string::npos ||
          name.find("lucida console") != std::string::npos) {
        family_name = "DejaVuSansMono";
        break;
      }
      if (name.find("sans") == std::string::npos &&
          (name.find("serif") != std::string::npos ||
           name.find("times") != std::string::npos ||
           name.find("georgia") != std::string::npos ||
           name.find("garamond") != std::string::npos ||
           name.find("palatino") != std::string::npos ||
           name.find("cambria") != std::string::npos ||
           name.find("book antiqua") != std::string::npos ||
           name.find("baskerville") != std::string::npos ||
           name.find("century") != std::string::npos)) {
        family_name = "DejaVuSerif";
        break;
      }
      if (name.find("sans") != std::string::npos ||
          name.find("arial") != std::string::npos ||
          name.find("helvetica") != std::string::npos ||
          name.find("verdana") != std::string::npos ||
          name.find("tahoma") != std::string::npos ||
          name.find("trebuchet") != std::string::npos ||
          name.find("segoe") != std::string::npos ||
          name.find("roboto") != std::string::npos ||
          name.find("inter") != std::string::npos ||
          name.find("system-ui") != std::string::npos) {
        family_name = "DejaVuSans";
        break;
      }
      if (name.find("math") != std::string::npos) {
        family_name = "DejaVuMathTeXGyre";
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
  const int dpi = browser_get_dpi();
  const float scale_factor = (dpi > 0) ? (static_cast<float>(dpi) / 72.0f) : (90.0f / 72.0f);
  float size = pt_size * scale_factor;
  if ((fstyle->flags & FONTF_SMALLCAPS) != 0) size *= kSmallCapsScale;

  int weight = fstyle->weight;
  if (weight <= 0) weight = 400;

  bool is_italic = (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) != 0;

  return ::perception::ui::GetUiFont(
      family_name, size, weight,
      is_italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant,
      SkFontStyle::kNormal_Width);
}

namespace {

size_t GetNextUtf8CharLength(std::string_view text_view, size_t index) {
  if (index >= text_view.length()) return 0;
  unsigned char byte_val = text_view[index];
  if ((byte_val & 0x80) == 0) return 1;
  if ((byte_val & 0xE0) == 0xC0) return 2;
  if ((byte_val & 0xF0) == 0xE0) return 3;
  if ((byte_val & 0xF8) == 0xF0) return 4;
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
  if (safe_len == 0 || x <= 0) {
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

  std::string_view text_view(text, safe_len);
  std::vector<size_t> offsets;
  offsets.reserve(safe_len + 1);
  offsets.push_back(0);
  size_t idx = 0;
  while (idx < safe_len) {
    size_t char_len = GetNextUtf8CharLength(text_view, idx);
    if (char_len == 0) break;
    idx += char_len;
    offsets.push_back(idx);
  }

  float total_advance =
      font->measureText(text, safe_len, SkTextEncoding::kUTF8);
  if (static_cast<float>(x) >= total_advance) {
    *char_offset = safe_len;
    *actual_x = static_cast<int>(std::round(total_advance));
    return NSERROR_OK;
  }

  size_t low = 0;
  size_t high = offsets.size() - 1;
  while (low < high) {
    size_t mid = low + (high - low) / 2;
    float advance =
        font->measureText(text, offsets[mid], SkTextEncoding::kUTF8);
    if (advance < static_cast<float>(x)) {
      low = mid + 1;
    } else {
      high = mid;
    }
  }

  float advance_high =
      font->measureText(text, offsets[low], SkTextEncoding::kUTF8);
  if (low > 0) {
    float advance_low =
        font->measureText(text, offsets[low - 1], SkTextEncoding::kUTF8);
    if (std::abs(advance_low - static_cast<float>(x)) <=
        std::abs(advance_high - static_cast<float>(x))) {
      *char_offset = offsets[low - 1];
      *actual_x = static_cast<int>(std::round(advance_low));
      return NSERROR_OK;
    }
  }

  *char_offset = offsets[low];
  *actual_x = static_cast<int>(std::round(advance_high));
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
    *char_offset = safe_len;
    *actual_x = 0;
    return NSERROR_OK;
  }

  float total_advance =
      font->measureText(text, safe_len, SkTextEncoding::kUTF8);
  if (total_advance <= static_cast<float>(x)) {
    *char_offset = safe_len;
    *actual_x = static_cast<int>(std::ceil(total_advance));
    return NSERROR_OK;
  }

  std::vector<size_t> space_indices;
  for (size_t i = 1; i + 1 < safe_len; ++i) {
    if (text[i] == ' ')
      space_indices.push_back(i);
  }

  if (space_indices.empty()) {
    *char_offset = safe_len;
    *actual_x = static_cast<int>(std::ceil(total_advance));
    return NSERROR_OK;
  }

  size_t first_space = space_indices.front();
  float first_space_advance =
      font->measureText(text, first_space, SkTextEncoding::kUTF8);
  if (first_space_advance > static_cast<float>(x)) {
    *char_offset = first_space;
    *actual_x = static_cast<int>(std::ceil(first_space_advance));
    return NSERROR_OK;
  }

  if (space_indices.size() == 1) {
    *char_offset = first_space;
    *actual_x = static_cast<int>(std::ceil(first_space_advance));
    return NSERROR_OK;
  }

  size_t last_space = space_indices.back();
  float last_space_advance =
      font->measureText(text, last_space, SkTextEncoding::kUTF8);
  if (last_space_advance <= static_cast<float>(x)) {
    *char_offset = last_space;
    *actual_x = static_cast<int>(std::ceil(last_space_advance));
    return NSERROR_OK;
  }

  size_t low = 0;
  size_t high = space_indices.size() - 1;
  float last_fit_advance = first_space_advance;
  while (low < high) {
    size_t mid = low + (high - low + 1) / 2;
    float mid_advance =
        font->measureText(text, space_indices[mid], SkTextEncoding::kUTF8);
    if (mid_advance <= static_cast<float>(x)) {
      low = mid;
      last_fit_advance = mid_advance;
    } else {
      high = mid - 1;
    }
  }

  *char_offset = space_indices[low];
  *actual_x = static_cast<int>(std::ceil(last_fit_advance));
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
