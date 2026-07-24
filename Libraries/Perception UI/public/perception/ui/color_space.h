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

#ifdef TEST
class SkColorSpace {};
template <typename T>
class sk_sp {};
struct skcms_TransferFunction {};
struct skcms_Matrix3x3 {};
#else
#include "include/core/SkColorSpace.h"
#include "modules/skcms/skcms.h"
#endif

namespace perception {
namespace ui {

enum class ColorSpaceTransferFn {
  SRGB = 0,
  TwoDotTwo = 1,
  Linear = 2,
  Rec2020 = 3,
  Rec709 = 4,
  Rec470SystemM = 5,
  Rec470SystemBG = 6,
  Rec601 = 7,
  SMPTE_ST_240 = 8,
  IEC61966_2_4 = 9,
  IEC61966_2_1 = 10,
  Rec2020_10bit = 11,
  Rec2020_12bit = 12,
  PQ = 13,
  SMPTE_ST_428_1 = 14,
  HLG = 15,
  ProPhotoRGB = 16,
  A98RGB = 17
};

enum class ColorSpaceGamut {
  SRGB = 0,
  AdobeRGB = 1,
  DisplayP3 = 2,
  Rec2020 = 3,
  XYZ = 4
};

skcms_TransferFunction GetSkTransferFunction(ColorSpaceTransferFn transfer_fn);
skcms_Matrix3x3 GetSkGamutMatrix(ColorSpaceGamut gamut);
sk_sp<SkColorSpace> CreateSkColorSpace(ColorSpaceTransferFn transfer_fn,
                                       ColorSpaceGamut gamut);

std::string SerializeColorSpace(const SkColorSpace* color_space);
sk_sp<SkColorSpace> DeserializeColorSpace(
    const std::string& serialized_color_space);

}  // namespace ui
}  // namespace perception
