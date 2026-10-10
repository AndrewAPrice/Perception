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

#include <string_view>

#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "types.h"

// Decodes DEC Sixel graphics sequences (`DCS P1;P2;P3 q <data> ST`) into an SkImage.
class SixelDecoder {
 public:
  // Decodes a complete DCS Sixel payload (starting with optional P1;P2;P3 parameters
  // before 'q') into an immutable SkImage, or returns nullptr if empty or invalid.
  static sk_sp<SkImage> Decode(std::string_view dcs_payload,
                               uint32 default_bg_color);
};
