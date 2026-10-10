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

#include "sixel_decoder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"

namespace {

// Maximum number of color registers supported by Sixel graphics.
constexpr size_t kMaxSixelColorRegisters = 256;

// Maximum horizontal pixel dimension allowed for a single Sixel bitmap.
constexpr int kMaxSixelDimension = 2048;

// Number of vertical pixels encoded by a single Sixel character byte.
constexpr int kSixelBandHeight = 6;

// Minimum ASCII byte value for a Sixel data character ('?').
constexpr char kSixelDataMinChar = '?';

// Maximum ASCII byte value for a Sixel data character ('~').
constexpr char kSixelDataMaxChar = '~';

// Default initial 16 VT340-style Sixel color registers in RGBA_8888 format.
constexpr std::array<uint32, 16> kDefaultSixelPalette = {
    0xFF181825,  // Register 0
    0xFFFA8B89,  // Register 1
    0xFFA88BFA,  // Register 2
    0xFFA1E3A6,  // Register 3
    0xFFAFC2F9,  // Register 4
    0xFFF7A6CB,  // Register 5
    0xFFD5E294,  // Register 6
    0xFFDEBAC2,  // Register 7
    0xFF705B58,  // Register 8
    0xFFFA8B89,  // Register 9
    0xFFA88BFA,  // Register 10
    0xFFA1E3A6,  // Register 11
    0xFFAFC2F9,  // Register 12
    0xFFE7C2F5,  // Register 13
    0xFFEBDC89,  // Register 14
    0xFFC8ADA6,  // Register 15
};

// Packs (R, G, B, A) into a little-endian kRGBA_8888_SkColorType word.
uint32 PackRgba8888(uint8 r, uint8 g, uint8 b, uint8 a = 0xFF) {
  return static_cast<uint32>(r) | (static_cast<uint32>(g) << 8) |
         (static_cast<uint32>(b) << 16) | (static_cast<uint32>(a) << 24);
}

// Converts an ARGB 0xAARRGGBB color word to kRGBA_8888_SkColorType.
uint32 ArgbToRgba8888(uint32 argb) {
  uint8 a = static_cast<uint8>((argb >> 24) & 0xFF);
  uint8 r = static_cast<uint8>((argb >> 16) & 0xFF);
  uint8 g = static_cast<uint8>((argb >> 8) & 0xFF);
  uint8 b = static_cast<uint8>(argb & 0xFF);
  return PackRgba8888(r, g, b, a);
}

// Converts Sixel HLS (hue 0..360, lightness 0..100, saturation 0..100) to RGBA_8888.
uint32 HlsToRgba8888(int h, int l, int s) {
  float hf = static_cast<float>(h % 360) / 360.0f;
  float lf = std::clamp(static_cast<float>(l) / 100.0f, 0.0f, 1.0f);
  float sf = std::clamp(static_cast<float>(s) / 100.0f, 0.0f, 1.0f);

  auto hue_to_rgb = [](float p, float q, float t) {
    if (t < 0.0f)
      t += 1.0f;
    if (t > 1.0f)
      t -= 1.0f;
    if (t < 1.0f / 6.0f)
      return p + (q - p) * 6.0f * t;
    if (t < 1.0f / 2.0f)
      return q;
    if (t < 2.0f / 3.0f)
      return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    return p;
  };

  float r = lf;
  float g = lf;
  float b = lf;
  if (sf > 0.0f) {
    float q = (lf < 0.5f) ? (lf * (1.0f + sf)) : (lf + sf - lf * sf);
    float p = 2.0f * lf - q;
    r = hue_to_rgb(p, q, hf + 1.0f / 3.0f);
    g = hue_to_rgb(p, q, hf);
    b = hue_to_rgb(p, q, hf - 1.0f / 3.0f);
  }
  return PackRgba8888(static_cast<uint8>(std::round(r * 255.0f)),
                      static_cast<uint8>(std::round(g * 255.0f)),
                      static_cast<uint8>(std::round(b * 255.0f)));
}

// Parses a non-negative integer from `sv` starting at `pos`.
int ParseNumber(std::string_view sv, size_t& pos) {
  int val = 0;
  while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') {
    val = val * 10 + (sv[pos] - '0');
    pos++;
  }
  return val;
}

// Parses semicolon-separated integer parameters from `sv` starting at `pos`.
void ParseParams(std::string_view sv, size_t& pos, std::vector<int>& out) {
  out.clear();
  while (pos < sv.size()) {
    if (sv[pos] >= '0' && sv[pos] <= '9') {
      out.push_back(ParseNumber(sv, pos));
      if (pos < sv.size() && sv[pos] == ';')
        pos++;
    } else if (sv[pos] == ';') {
      out.push_back(0);
      pos++;
    } else {
      break;
    }
  }
}

}  // namespace

sk_sp<SkImage> SixelDecoder::Decode(std::string_view dcs_payload,
                                    uint32 default_bg_color) {
  size_t q_pos = dcs_payload.find('q');
  if (q_pos == std::string_view::npos)
    return nullptr;

  size_t header_pos = 0;
  std::vector<int> header_params;
  ParseParams(dcs_payload.substr(0, q_pos), header_pos, header_params);
  int bg_mode = (header_params.size() >= 2) ? header_params[1] : 0;

  std::array<uint32, kMaxSixelColorRegisters> palette;
  palette.fill(PackRgba8888(0xCD, 0xD6, 0xF4));
  for (size_t i = 0; i < kDefaultSixelPalette.size(); ++i)
    palette[i] = kDefaultSixelPalette[i];

  int canvas_w = 0;
  int canvas_h = 0;
  std::vector<uint32> pixels;
  uint32 fill_pixel = (bg_mode == 1) ? 0u : ArgbToRgba8888(default_bg_color);

  auto ensure_size = [&](int req_w, int req_h) {
    req_w = std::min(req_w, kMaxSixelDimension);
    req_h = std::min(req_h, kMaxSixelDimension);
    if (req_w <= canvas_w && req_h <= canvas_h)
      return;
    int new_w = std::max(canvas_w, req_w);
    int new_h = std::max(canvas_h, req_h);
    std::vector<uint32> new_pixels(static_cast<size_t>(new_w) * new_h,
                                   fill_pixel);
    for (int y = 0; y < canvas_h; ++y) {
      for (int x = 0; x < canvas_w; ++x)
        new_pixels[y * new_w + x] = pixels[y * canvas_w + x];
    }
    pixels = std::move(new_pixels);
    canvas_w = new_w;
    canvas_h = new_h;
  };

  int current_color_reg = 0;
  int x = 0;
  int band_y = 0;
  int max_drawn_x = 0;
  int max_drawn_y = 0;

  size_t pos = q_pos + 1;
  std::vector<int> params;
  while (pos < dcs_payload.size()) {
    char ch = dcs_payload[pos];
    if (ch >= kSixelDataMinChar && ch <= kSixelDataMaxChar) {
      pos++;
      uint8 bits = static_cast<uint8>(ch - kSixelDataMinChar);
      ensure_size(x + 1, band_y + kSixelBandHeight);
      if (x < canvas_w) {
        uint32 color = palette[current_color_reg];
        for (int b = 0; b < kSixelBandHeight; ++b) {
          if (((bits >> b) & 1) && (band_y + b) < canvas_h)
            pixels[(band_y + b) * canvas_w + x] = color;
        }
        x++;
        max_drawn_x = std::max(max_drawn_x, x);
        max_drawn_y = std::max(max_drawn_y, band_y + kSixelBandHeight);
      }
    } else if (ch == '!') {
      pos++;
      int repeat_count = std::max(1, ParseNumber(dcs_payload, pos));
      if (pos < dcs_payload.size()) {
        char data_ch = dcs_payload[pos];
        if (data_ch >= kSixelDataMinChar && data_ch <= kSixelDataMaxChar) {
          pos++;
          uint8 bits = static_cast<uint8>(data_ch - kSixelDataMinChar);
          ensure_size(x + repeat_count, band_y + kSixelBandHeight);
          uint32 color = palette[current_color_reg];
          for (int r = 0; r < repeat_count && x < canvas_w; ++r) {
            for (int b = 0; b < kSixelBandHeight; ++b) {
              if (((bits >> b) & 1) && (band_y + b) < canvas_h)
                pixels[(band_y + b) * canvas_w + x] = color;
            }
            x++;
          }
          max_drawn_x = std::max(max_drawn_x, x);
          max_drawn_y = std::max(max_drawn_y, band_y + kSixelBandHeight);
        }
      }
    } else if (ch == '$') {
      pos++;
      x = 0;
    } else if (ch == '-') {
      pos++;
      x = 0;
      band_y += kSixelBandHeight;
    } else if (ch == '#') {
      pos++;
      ParseParams(dcs_payload, pos, params);
      if (!params.empty()) {
        current_color_reg =
            std::clamp(params[0], 0, static_cast<int>(kMaxSixelColorRegisters) - 1);
        if (params.size() >= 5) {
          int mode = params[1];
          int p1 = params[2];
          int p2 = params[3];
          int p3 = params[4];
          if (mode == 2) {
            uint8 r = static_cast<uint8>(std::clamp(p1, 0, 100) * 255 / 100);
            uint8 g = static_cast<uint8>(std::clamp(p2, 0, 100) * 255 / 100);
            uint8 b = static_cast<uint8>(std::clamp(p3, 0, 100) * 255 / 100);
            palette[current_color_reg] = PackRgba8888(r, g, b);
          } else if (mode == 1) {
            palette[current_color_reg] = HlsToRgba8888(p1, p2, p3);
          }
        }
      }
    } else if (ch == '"') {
      pos++;
      ParseParams(dcs_payload, pos, params);
      if (params.size() >= 4) {
        int decl_w = std::clamp(params[2], 0, kMaxSixelDimension);
        int decl_h = std::clamp(params[3], 0, kMaxSixelDimension);
        if (decl_w > 0 && decl_h > 0) {
          ensure_size(decl_w, decl_h);
          max_drawn_x = std::max(max_drawn_x, decl_w);
          max_drawn_y = std::max(max_drawn_y, decl_h);
        }
      }
    } else {
      pos++;
    }
  }

  int final_w = std::min(canvas_w, max_drawn_x);
  int final_h = std::min(canvas_h, max_drawn_y);
  if (final_w <= 0 || final_h <= 0)
    return nullptr;

  SkImageInfo info = SkImageInfo::Make(final_w, final_h,
                                       SkColorType::kRGBA_8888_SkColorType,
                                       SkAlphaType::kUnpremul_SkAlphaType);
  SkPixmap pixmap(info, pixels.data(), static_cast<size_t>(canvas_w) * 4);
  return SkImages::RasterFromPixmapCopy(pixmap);
}
