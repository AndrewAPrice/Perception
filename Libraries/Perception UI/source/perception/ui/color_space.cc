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

#include "perception/ui/color_space.h"
#include "include/core/SkData.h"

namespace perception {
namespace ui {

skcms_TransferFunction GetSkTransferFunction(ColorSpaceTransferFn transfer_fn) {
  switch (transfer_fn) {
    case ColorSpaceTransferFn::SRGB:
      return SkNamedTransferFn::kSRGB;
    case ColorSpaceTransferFn::TwoDotTwo:
      return SkNamedTransferFn::k2Dot2;
    case ColorSpaceTransferFn::Linear:
      return SkNamedTransferFn::kLinear;
    case ColorSpaceTransferFn::Rec2020:
      return SkNamedTransferFn::kRec2020;
    case ColorSpaceTransferFn::Rec709:
      return SkNamedTransferFn::kRec709;
    case ColorSpaceTransferFn::Rec470SystemM:
      return SkNamedTransferFn::kRec470SystemM;
    case ColorSpaceTransferFn::Rec470SystemBG:
      return SkNamedTransferFn::kRec470SystemBG;
    case ColorSpaceTransferFn::Rec601:
      return SkNamedTransferFn::kRec601;
    case ColorSpaceTransferFn::SMPTE_ST_240:
      return SkNamedTransferFn::kSMPTE_ST_240;
    case ColorSpaceTransferFn::IEC61966_2_4:
      return SkNamedTransferFn::kIEC61966_2_4;
    case ColorSpaceTransferFn::IEC61966_2_1:
      return SkNamedTransferFn::kIEC61966_2_1;
    case ColorSpaceTransferFn::Rec2020_10bit:
      return SkNamedTransferFn::kRec2020_10bit;
    case ColorSpaceTransferFn::Rec2020_12bit:
      return SkNamedTransferFn::kRec2020_12bit;
    case ColorSpaceTransferFn::PQ:
      return SkNamedTransferFn::kPQ;
    case ColorSpaceTransferFn::SMPTE_ST_428_1:
      return SkNamedTransferFn::kSMPTE_ST_428_1;
    case ColorSpaceTransferFn::HLG:
      return SkNamedTransferFn::kHLG;
    case ColorSpaceTransferFn::ProPhotoRGB:
      return SkNamedTransferFn::kProPhotoRGB;
    case ColorSpaceTransferFn::A98RGB:
      return SkNamedTransferFn::kA98RGB;
    default:
      return SkNamedTransferFn::kSRGB;
  }
}

skcms_Matrix3x3 GetSkGamutMatrix(ColorSpaceGamut gamut) {
  switch (gamut) {
    case ColorSpaceGamut::SRGB:
      return SkNamedGamut::kSRGB;
    case ColorSpaceGamut::AdobeRGB:
      return SkNamedGamut::kAdobeRGB;
    case ColorSpaceGamut::DisplayP3:
      return SkNamedGamut::kDisplayP3;
    case ColorSpaceGamut::Rec2020:
      return SkNamedGamut::kRec2020;
    case ColorSpaceGamut::XYZ:
      return SkNamedGamut::kXYZ;
    default:
      return SkNamedGamut::kSRGB;
  }
}

sk_sp<SkColorSpace> CreateSkColorSpace(ColorSpaceTransferFn transfer_fn,
                                       ColorSpaceGamut gamut) {
  return SkColorSpace::MakeRGB(GetSkTransferFunction(transfer_fn),
                               GetSkGamutMatrix(gamut));
}

std::string SerializeColorSpace(const SkColorSpace* color_space) {
  if (!color_space) return "";
  auto data = color_space->serialize();
  if (!data) return "";
  return std::string(static_cast<const char*>(data->data()), data->size());
}

sk_sp<SkColorSpace> DeserializeColorSpace(
    const std::string& serialized_color_space) {
  if (serialized_color_space.empty()) return nullptr;
  return SkColorSpace::Deserialize(serialized_color_space.data(),
                                   serialized_color_space.size());
}

}  // namespace ui
}  // namespace perception
