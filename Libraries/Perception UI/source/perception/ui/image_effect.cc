// Copyright 2024 Google LLC
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

#include "perception/ui/image_effect.h"

#include <functional>
#include <iostream>
#include <map>

#include "include/core/SkBlendMode.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkM44.h"
#include "include/effects/SkColorMatrix.h"
#include "include/effects/SkImageFilters.h"

namespace perception {
namespace ui {
namespace {

constexpr float kBlurSizeToSigma = 1.0f;

std::map<std::string, std::weak_ptr<ImageEffect>> cached_image_effects;

std::shared_ptr<ImageEffect> ReturnCachedImageEffectOrCreate(
    std::string& key, const std::function<sk_sp<SkImageFilter>()>& create) {
  auto itr = cached_image_effects.find(key);
  if (itr != cached_image_effects.end()) {
    if (auto image_effect = itr->second.lock()) return image_effect;
    // It has already been deallocated, so replace it with a new one.
    cached_image_effects.erase(itr);
  }

  auto image_effect = std::make_shared<ImageEffect>(key, create());
  cached_image_effects[key] = image_effect;
  return image_effect;
}

sk_sp<SkImageFilter> CreateShadowImageFilter(bool inner_shadow, bool only,
                                             uint32_t color, float opacity,
                                             float angle, float size,
                                             float distance) {
  // Inspiration is from Skia's own sources in
  // modules/skottie/src/effects/ShadowStyles.cpp.
  float rad = SkDegreesToRadians(angle);
  float sigma = size * kBlurSizeToSigma;
  SkColor4f sk_color = SkColor4f::FromBytes_RGBA(color);
  SkV2 offset = SkV2{distance * SkScalarCos(rad), -distance * SkScalarSin(rad)};

  // Select and colorize the source alpha channel.
  SkColorMatrix cm{0,
                   0,
                   0,
                   0,
                   sk_color.fR,
                   0,
                   0,
                   0,
                   0,
                   sk_color.fG,
                   0,
                   0,
                   0,
                   0,
                   sk_color.fB,
                   0,
                   0,
                   0,
                   opacity * sk_color.fA,
                   0};
  // Inner shadows use the alpha inverse.
  if (inner_shadow)
    cm.preConcat({1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, -1, 1});
  auto f = SkImageFilters::ColorFilter(SkColorFilters::Matrix(cm), nullptr);
  if (sigma > 0) f = SkImageFilters::Blur(sigma, sigma, std::move(f));
  if (!SkScalarNearlyZero(offset.x) || !SkScalarNearlyZero(offset.y)) {
    f = SkImageFilters::Offset(offset.x, offset.y, std::move(f));
  }
  sk_sp<SkImageFilter> source;
  if (inner_shadow) {
    // Inner shadows draw on top of, and are masked with, the source.
    f = SkImageFilters::Blend(SkBlendMode::kDstIn, std::move(f));
    std::swap(source, f);
  }

  return SkImageFilters::Merge(std::move(f), std::move(source));
}

}  // namespace

ImageEffect::ImageEffect(std::string key,
                         sk_sp<SkImageFilter> skia_image_filter)
    : key_(key), skia_image_filter_(skia_image_filter) {}

ImageEffect::~ImageEffect() { cached_image_effects.erase(key_); }

std::shared_ptr<ImageEffect> ImageEffect::DropShadow(uint32_t color,
                                                     float opacity, float angle,
                                                     float size,
                                                     float distance) {
  std::string key = "ds_" + std::to_string(color) + "_" +
                    std::to_string(opacity) + "_" + std::to_string(angle) +
                    "_" + std::to_string(size) + "_" + std::to_string(distance);
  return ReturnCachedImageEffectOrCreate(key, [&]() {
    return CreateShadowImageFilter(/*inner_shadow=*/false, /*only=*/false,
                                   color, opacity, angle, size, distance);
  });
}

std::shared_ptr<ImageEffect> ImageEffect::InnerShadow(uint32_t color,
                                                      float opacity,
                                                      float angle, float size,
                                                      float distance) {
  std::string key = "is_" + std::to_string(color) + "_" +
                    std::to_string(opacity) + "_" + std::to_string(angle) +
                    "_" + std::to_string(size) + "_" + std::to_string(distance);
  return ReturnCachedImageEffectOrCreate(key, [&]() {
    return CreateShadowImageFilter(/*inner_shadow=*/true, /*only=*/false, color,
                                   opacity, angle, size, distance);
  });
}

std::shared_ptr<ImageEffect> ImageEffect::InnerShadowOnly(
    uint32_t color, float opacity, float angle, float size, float distance) {
  std::string key = "iso_" + std::to_string(color) + "_" +
                    std::to_string(opacity) + "_" + std::to_string(angle) +
                    "_" + std::to_string(size) + "_" + std::to_string(distance);
  return ReturnCachedImageEffectOrCreate(key, [&]() {
    return CreateShadowImageFilter(/*inner_shadow=*/true, /*only=*/true, color,
                                   opacity, angle, size, distance);
  });
}

}  // namespace ui
}  // namespace perception
