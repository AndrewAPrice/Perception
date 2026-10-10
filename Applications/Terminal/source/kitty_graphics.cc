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

#include "kitty_graphics.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"

namespace {

// Format code for 24-bit RGB pixel data in the Kitty Graphics Protocol.
constexpr int kKittyFormatRgb24 = 24;

// Format code for 32-bit RGBA pixel data in the Kitty Graphics Protocol.
constexpr int kKittyFormatRgba32 = 32;

// Format code for PNG-encoded data in the Kitty Graphics Protocol.
constexpr int kKittyFormatPng = 100;

// Maximum number of cached Kitty images before evicting older entries.
constexpr size_t kMaxCachedKittyImages = 256;

// Appends base64-decoded bytes from `encoded` into `out`.
void AppendBase64Decoded(std::string_view encoded, std::vector<uint8>& out) {
  uint32 accum = 0;
  int bits = 0;
  for (char c : encoded) {
    int val = -1;
    if (c >= 'A' && c <= 'Z') {
      val = c - 'A';
    } else if (c >= 'a' && c <= 'z') {
      val = c - 'a' + 26;
    } else if (c >= '0' && c <= '9') {
      val = c - '0' + 52;
    } else if (c == '+') {
      val = 62;
    } else if (c == '/') {
      val = 63;
    } else if (c == '=') {
      break;
    } else {
      continue;
    }
    accum = (accum << 6) | static_cast<uint32>(val);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<uint8>((accum >> bits) & 0xFF));
    }
  }
}

// Parses a signed integer from `sv`.
int32 ParseSignedInt(std::string_view sv) {
  if (sv.empty())
    return 0;
  bool neg = false;
  size_t pos = 0;
  if (sv[0] == '-') {
    neg = true;
    pos = 1;
  } else if (sv[0] == '+') {
    pos = 1;
  }
  int64 val = 0;
  while (pos < sv.size() && sv[pos] >= '0' && sv[pos] <= '9') {
    val = val * 10 + (sv[pos] - '0');
    pos++;
  }
  return static_cast<int32>(neg ? -val : val);
}

// Formats a Kitty Graphics Protocol response (`\x1b_Gi=...;status\x1b\\`).
std::string FormatKittyResponse(uint32 image_id, uint32 placement_id,
                                std::string_view status) {
  std::string resp = "\x1b_Gi=";
  resp += std::to_string(image_id);
  if (placement_id != 0) {
    resp += ",p=";
    resp += std::to_string(placement_id);
  }
  resp += ';';
  resp.append(status);
  resp += "\x1b\\";
  return resp;
}

}  // namespace

KittyGraphics::KittyGraphics()
    : next_auto_image_id_(100000), in_chunked_transfer_(false) {}

void KittyGraphics::Reset() {
  images_.clear();
  in_chunked_transfer_ = false;
  pending_data_.clear();
}

void KittyGraphics::HandleCommand(
    std::string_view payload, TerminalBuffer& buffer, float cell_width,
    float cell_height,
    const std::function<void(std::string_view)>& send_response) {
  size_t semi = payload.find(';');
  std::string_view control_str =
      (semi == std::string_view::npos) ? payload : payload.substr(0, semi);
  std::string_view b64_str =
      (semi == std::string_view::npos) ? "" : payload.substr(semi + 1);

  CommandParams params = in_chunked_transfer_ ? chunked_params_ : CommandParams{};
  if (in_chunked_transfer_)
    params.more_chunks = 0;

  size_t pos = 0;
  while (pos < control_str.size()) {
    size_t comma = control_str.find(',', pos);
    std::string_view kv =
        (comma == std::string_view::npos)
            ? control_str.substr(pos)
            : control_str.substr(pos, comma - pos);
    pos = (comma == std::string_view::npos) ? control_str.size() : (comma + 1);

    if (kv.size() < 3 || kv[1] != '=')
      continue;
    char key = kv[0];
    std::string_view val = kv.substr(2);
    switch (key) {
      case 'a':
        if (!val.empty())
          params.action = val[0];
        break;
      case 'd':
        if (!val.empty())
          params.delete_target = val[0];
        break;
      case 'f':
        params.format = ParseSignedInt(val);
        break;
      case 's':
        params.width = ParseSignedInt(val);
        break;
      case 'v':
        params.height = ParseSignedInt(val);
        break;
      case 'i':
        params.image_id = static_cast<uint32>(std::max(0, ParseSignedInt(val)));
        break;
      case 'p':
        params.placement_id =
            static_cast<uint32>(std::max(0, ParseSignedInt(val)));
        break;
      case 'm':
        params.more_chunks = ParseSignedInt(val);
        break;
      case 'q':
        params.quiet = ParseSignedInt(val);
        break;
      case 'c':
        params.cols = ParseSignedInt(val);
        break;
      case 'r':
        params.rows = ParseSignedInt(val);
        break;
      case 'x':
        params.crop_x = ParseSignedInt(val);
        break;
      case 'y':
        params.crop_y = ParseSignedInt(val);
        break;
      case 'w':
        params.crop_w = ParseSignedInt(val);
        break;
      case 'h':
        params.crop_h = ParseSignedInt(val);
        break;
      case 'X':
        params.x_offset = ParseSignedInt(val);
        break;
      case 'Y':
        params.y_offset = ParseSignedInt(val);
        break;
      case 'z':
        params.z_index = ParseSignedInt(val);
        break;
      case 'C':
        params.cursor_movement = ParseSignedInt(val);
        break;
      default:
        break;
    }
  }

  if (params.action == 'd') {
    buffer.DeleteGraphicPlacements(params.delete_target, params.image_id,
                                   params.placement_id, params.z_index);
    if ((params.delete_target == 'I' || params.delete_target == 'A') &&
        params.image_id != 0) {
      images_.erase(params.image_id);
    }
    return;
  }

  if (params.action == 'p') {
    auto it = images_.find(params.image_id);
    if (it != images_.end() && it->second) {
      PlaceImage(params, it->second, buffer, cell_width, cell_height);
      if (params.image_id != 0 && params.quiet == 0 && send_response) {
        send_response(
            FormatKittyResponse(params.image_id, params.placement_id, "OK"));
      }
    } else if (params.image_id != 0 && params.quiet < 2 && send_response) {
      send_response(FormatKittyResponse(params.image_id, params.placement_id,
                                        "ENOENT:image not found"));
    }
    return;
  }

  if (!in_chunked_transfer_)
    pending_data_.clear();
  AppendBase64Decoded(b64_str, pending_data_);

  if (params.more_chunks == 1) {
    in_chunked_transfer_ = true;
    chunked_params_ = params;
    return;
  }
  in_chunked_transfer_ = false;

  sk_sp<SkImage> decoded = DecodeImage(params);
  pending_data_.clear();

  if (!decoded) {
    if (params.image_id != 0 && params.quiet < 2 && send_response) {
      send_response(FormatKittyResponse(params.image_id, params.placement_id,
                                        "EINVAL:failed to decode image"));
    }
    return;
  }

  if (params.action != 'q') {
    uint32 store_id = params.image_id;
    if (store_id == 0)
      store_id = next_auto_image_id_++;
    if (images_.size() >= kMaxCachedKittyImages)
      images_.erase(images_.begin());
    images_[store_id] = decoded;

    if (params.action == 'T')
      PlaceImage(params, decoded, buffer, cell_width, cell_height);
  }

  if (params.image_id != 0 && params.quiet == 0 && send_response) {
    send_response(
        FormatKittyResponse(params.image_id, params.placement_id, "OK"));
  }
}

sk_sp<SkImage> KittyGraphics::DecodeImage(const CommandParams& params) const {
  if (pending_data_.empty())
    return nullptr;

  if (params.format == kKittyFormatPng) {
    sk_sp<SkData> sk_data =
        SkData::MakeWithCopy(pending_data_.data(), pending_data_.size());
    return SkImages::DeferredFromEncodedData(sk_data, std::nullopt);
  }

  if (params.width <= 0 || params.height <= 0)
    return nullptr;

  size_t pixel_count =
      static_cast<size_t>(params.width) * static_cast<size_t>(params.height);
  if (params.format == kKittyFormatRgba32) {
    if (pending_data_.size() < pixel_count * 4)
      return nullptr;
    SkImageInfo info = SkImageInfo::Make(params.width, params.height,
                                         SkColorType::kRGBA_8888_SkColorType,
                                         SkAlphaType::kUnpremul_SkAlphaType);
    SkPixmap pixmap(info, pending_data_.data(),
                    static_cast<size_t>(params.width) * 4);
    return SkImages::RasterFromPixmapCopy(pixmap);
  }

  if (params.format == kKittyFormatRgb24) {
    if (pending_data_.size() < pixel_count * 3)
      return nullptr;
    std::vector<uint8> rgba(pixel_count * 4);
    for (size_t i = 0; i < pixel_count; ++i) {
      rgba[i * 4 + 0] = pending_data_[i * 3 + 0];
      rgba[i * 4 + 1] = pending_data_[i * 3 + 1];
      rgba[i * 4 + 2] = pending_data_[i * 3 + 2];
      rgba[i * 4 + 3] = 0xFF;
    }
    SkImageInfo info = SkImageInfo::Make(params.width, params.height,
                                         SkColorType::kRGBA_8888_SkColorType,
                                         SkAlphaType::kOpaque_SkAlphaType);
    SkPixmap pixmap(info, rgba.data(), static_cast<size_t>(params.width) * 4);
    return SkImages::RasterFromPixmapCopy(pixmap);
  }

  return nullptr;
}

void KittyGraphics::PlaceImage(const CommandParams& params,
                               const sk_sp<SkImage>& image,
                               TerminalBuffer& buffer, float cell_width,
                               float cell_height) const {
  if (!image)
    return;

  float cw = std::max(1.0f, cell_width);
  float ch = std::max(1.0f, cell_height);
  int occupied_cols =
      params.cols > 0
          ? params.cols
          : std::max(1, static_cast<int>(std::ceil(image->width() / cw)));
  int occupied_rows =
      params.rows > 0
          ? params.rows
          : std::max(1, static_cast<int>(std::ceil(image->height() / ch)));

  while (buffer.CursorRow() + occupied_rows > buffer.Rows()) {
    buffer.ScrollUp(1);
    if (buffer.CursorRow() > 0)
      buffer.SetRawCursorPos(buffer.CursorRow() - 1, buffer.CursorCol());
  }

  TerminalGraphicPlacement placement;
  placement.image_id = params.image_id;
  placement.placement_id = params.placement_id;
  placement.image = image;
  placement.anchor_line_id = buffer.CurrentCursorLineId();
  placement.anchor_col = buffer.CursorCol();
  placement.display_cols = params.cols;
  placement.display_rows = params.rows;
  placement.crop_x = params.crop_x;
  placement.crop_y = params.crop_y;
  placement.crop_w = params.crop_w;
  placement.crop_h = params.crop_h;
  placement.x_offset = params.x_offset;
  placement.y_offset = params.y_offset;
  placement.z_index = params.z_index;
  placement.is_alt_screen = buffer.IsAlternateScreen();
  buffer.AddGraphicPlacement(placement);

  if (params.cursor_movement == 0) {
    for (int r = 0; r < occupied_rows; ++r)
      buffer.NewLine(true);
  }
}
