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

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "terminal_buffer.h"
#include "types.h"

// Decodes and manages images and placements for the Kitty Graphics Protocol (`APC G ... ST`).
class KittyGraphics {
 public:
  // Constructs a KittyGraphics manager.
  KittyGraphics();

  // Resets all stored images and in-progress chunked transfers.
  void Reset();

  // Processes a single Kitty Graphics APC payload (after the leading 'G') and
  // updates `buffer`, sending any protocol response via `send_response`.
  void HandleCommand(std::string_view payload, TerminalBuffer& buffer,
                     float cell_width, float cell_height,
                     const std::function<void(std::string_view)>& send_response);

 private:
  // Stores parsed control parameters for a Kitty graphics command.
  struct CommandParams {
    char action = 't';
    char delete_target = 'a';
    int format = 32;
    int width = 0;
    int height = 0;
    uint32 image_id = 0;
    uint32 placement_id = 0;
    int more_chunks = 0;
    int quiet = 0;
    int cols = 0;
    int rows = 0;
    int crop_x = 0;
    int crop_y = 0;
    int crop_w = 0;
    int crop_h = 0;
    int x_offset = 0;
    int y_offset = 0;
    int32 z_index = 0;
    int cursor_movement = 0;
  };

  // Decodes `pending_data_` into an SkImage according to `params`.
  sk_sp<SkImage> DecodeImage(const CommandParams& params) const;

  // Places an image into `buffer` and advances the cursor if requested.
  void PlaceImage(const CommandParams& params, const sk_sp<SkImage>& image,
                  TerminalBuffer& buffer, float cell_width,
                  float cell_height) const;

  std::unordered_map<uint32, sk_sp<SkImage>> images_;
  uint32 next_auto_image_id_;
  bool in_chunked_transfer_;
  CommandParams chunked_params_;
  std::vector<uint8> pending_data_;
};
