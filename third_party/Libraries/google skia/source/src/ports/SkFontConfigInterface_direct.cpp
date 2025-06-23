/*
 * Copyright 2009-2015 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/* migrated from chrome/src/skia/ext/SkFontHost_fontconfig_direct.cpp */

#include "src/ports/SkFontConfigInterface_direct.h"

#include <unistd.h>

#include <iostream>
#include <map>

#include "include/core/SkFontStyle.h"
#include "include/core/SkStream.h"
#include "include/core/SkString.h"
#include "include/core/SkTypeface.h"
#include "include/private/base/SkFixed.h"
#include "include/private/base/SkMutex.h"
#include "include/private/base/SkTArray.h"
#include "include/private/base/SkTDArray.h"
#include "include/private/base/SkTemplates.h"
#include "perception/services.h"
#include "perception/ui/font_manager.h"
#include "src/base/SkAutoMalloc.h"
#include "src/base/SkBuffer.h"
#include "src/ports/SkFontConfigInterface_direct.h"

using ::perception::GetService;
using ::perception::SharedMemory;
using ::perception::ui::FontData;
using ::perception::ui::FontFamilies;
using ::perception::ui::FontFamily;
using ::perception::ui::FontManager;
using ::perception::ui::FontStyle;
using ::perception::ui::FontStyles;
using ::perception::ui::MatchFontRequest;
using ::perception::ui::MatchFontResponse;
using SkFS = SkFontStyle;

namespace {
auto kFontWeightToSkiaWeight = std::map<FontStyle::Weight, int>(
    {{FontStyle::Weight::THIN, SkFS::kThin_Weight},
     {FontStyle::Weight::EXTRALIGHT, SkFS::kExtraLight_Weight},
     {FontStyle::Weight::LIGHT, SkFS::kLight_Weight},
     {FontStyle::Weight::SEMILIGHT, 350},
     {FontStyle::Weight::BOOK, 380},
     {FontStyle::Weight::REGULAR, SkFS::kNormal_Weight},
     {FontStyle::Weight::MEDIUM, SkFS::kMedium_Weight},
     {FontStyle::Weight::SEMIBOLD, SkFS::kSemiBold_Weight},
     {FontStyle::Weight::BOLD, SkFS::kBold_Weight},
     {FontStyle::Weight::EXTRABOLD, SkFS::kExtraBold_Weight},
     {FontStyle::Weight::BLACK, SkFS::kBlack_Weight},
     {FontStyle::Weight::EXTRABLACK, SkFS::kExtraBlack_Weight}});

auto kFontWidthToSkiaWidth = std::map<FontStyle::Width, SkFS::Width>(
    {{FontStyle::Width::ULTRACONDENSED, SkFS::kUltraCondensed_Width},
     {FontStyle::Width::EXTRACONDENSED, SkFS::kExtraCondensed_Width},
     {FontStyle::Width::CONDENSED, SkFS::kCondensed_Width},
     {FontStyle::Width::SEMICONDENSED, SkFS::kSemiCondensed_Width},
     {FontStyle::Width::NORMAL, SkFS::kNormal_Width},
     {FontStyle::Width::SEMIEXPANDED, SkFS::kSemiExpanded_Width},
     {FontStyle::Width::EXPANDED, SkFS::kExpanded_Width},
     {FontStyle::Width::EXTRAEXPANDED, SkFS::kExtraExpanded_Width},
     {FontStyle::Width::ULTRAEXPANDED, SkFS::kUltraExpanded_Width}});

auto kFontSlantToSkiaSlant = std::map<FontStyle::Slant, SkFS::Slant>(
    {{FontStyle::Slant::UPRIGHT, SkFS::kUpright_Slant},
     {FontStyle::Slant::ITALIC, SkFS::kItalic_Slant},
     {FontStyle::Slant::OBLIQUE, SkFS::kOblique_Slant}});

static int map_range(SkScalar value, SkScalar old_min, SkScalar old_max,
                     SkScalar new_min, SkScalar new_max) {
  SkASSERT(old_min < old_max);
  SkASSERT(new_min <= new_max);
  return new_min +
         ((value - old_min) * (new_max - new_min) / (old_max - old_min));
}

struct MapRanges {
  SkScalar old_val;
  SkScalar new_val;
};

static SkScalar map_ranges(SkScalar val, MapRanges const ranges[],
                           int rangesCount) {
  // -Inf to [0]
  if (val < ranges[0].old_val) {
    return ranges[0].new_val;
  }

  // Linear from [i] to [i+1]
  for (int i = 0; i < rangesCount - 1; ++i) {
    if (val < ranges[i + 1].old_val) {
      return map_range(val, ranges[i].old_val, ranges[i + 1].old_val,
                       ranges[i].new_val, ranges[i + 1].new_val);
    }
  }

  // From [n] to +Inf
  // if (fcweight < Inf)
  return ranges[rangesCount - 1].new_val;
}

template <typename K, typename V>
V GetOrDefault(const std::map<K, V>& m, const K key, const V default_value) {
  typename std::map<K, V>::const_iterator it = m.find(key);
  return it == m.end() ? default_value : it->second;
}

SkFontStyle SkFontStyleFromFontStyle(const FontStyle& font_style) {
  typedef SkFontStyle SkFS;

  int weight = (int)GetOrDefault(kFontWeightToSkiaWeight, font_style.weight,
                                 (int)SkFS::kNormal_Weight);
  int width = (int)GetOrDefault(kFontWidthToSkiaWidth, font_style.width,
                                SkFS::kNormal_Width);
  SkFS::Slant slant = GetOrDefault(kFontSlantToSkiaSlant, font_style.slant,
                                   SkFS::kUpright_Slant);

  return SkFontStyle(weight, width, slant);
}

void FontStyleFromSkFontStyle(SkFontStyle style, FontStyle& font_style) {
  typedef SkFontStyle SkFS;

  static constexpr MapRanges weight_ranges[] = {
      {SkFS::kThin_Weight, (SkScalar)FontStyle::Weight::THIN},
      {SkFS::kExtraLight_Weight, (SkScalar)FontStyle::Weight::EXTRALIGHT},
      {SkFS::kLight_Weight, (SkScalar)FontStyle::Weight::LIGHT},
      {350, (SkScalar)FontStyle::Weight::SEMILIGHT},
      {380, (SkScalar)FontStyle::Weight::BOOK},
      {SkFS::kNormal_Weight, (SkScalar)FontStyle::Weight::REGULAR},
      {SkFS::kMedium_Weight, (SkScalar)FontStyle::Weight::MEDIUM},
      {SkFS::kSemiBold_Weight, (SkScalar)FontStyle::Weight::SEMIBOLD},
      {SkFS::kBold_Weight, (SkScalar)FontStyle::Weight::BOLD},
      {SkFS::kExtraBold_Weight, (SkScalar)FontStyle::Weight::EXTRABOLD},
      {SkFS::kBlack_Weight, (SkScalar)FontStyle::Weight::BLACK},
      {SkFS::kExtraBlack_Weight, (SkScalar)FontStyle::Weight::EXTRABLACK},
  };
  font_style.weight = (FontStyle::Weight)SkScalarRoundToInt(
      map_ranges(style.weight(), weight_ranges, std::size(weight_ranges)));

  static constexpr MapRanges width_ranges[] = {
      {SkFS::kUltraCondensed_Width, (SkScalar)FontStyle::Width::ULTRACONDENSED},
      {SkFS::kExtraCondensed_Width, (SkScalar)FontStyle::Width::EXTRACONDENSED},
      {SkFS::kCondensed_Width, (SkScalar)FontStyle::Width::CONDENSED},
      {SkFS::kSemiCondensed_Width, (SkScalar)FontStyle::Width::SEMICONDENSED},
      {SkFS::kNormal_Width, (SkScalar)FontStyle::Width::NORMAL},
      {SkFS::kSemiExpanded_Width, (SkScalar)FontStyle::Width::SEMIEXPANDED},
      {SkFS::kExpanded_Width, (SkScalar)FontStyle::Width::EXPANDED},
      {SkFS::kExtraExpanded_Width, (SkScalar)FontStyle::Width::EXTRAEXPANDED},
      {SkFS::kUltraExpanded_Width, (SkScalar)FontStyle::Width::ULTRAEXPANDED},
  };
  font_style.width = (FontStyle::Width)SkScalarRoundToInt(
      map_ranges(style.width(), width_ranges, std::size(width_ranges)));

  switch (style.slant()) {
    case SkFS::kUpright_Slant:
      font_style.slant = FontStyle::Slant::UPRIGHT;
      break;
    case SkFS::kItalic_Slant:
      font_style.slant = FontStyle::Slant::ITALIC;
      break;
    case SkFS::kOblique_Slant:
      font_style.slant = FontStyle::Slant::OBLIQUE;
      break;
    default:
      SkASSERT(false);
      break;
  }
}

}  // anonymous namespace

///////////////////////////////////////////////////////////////////////////////

SkFontConfigInterfaceDirect::SkFontConfigInterfaceDirect() {}

SkFontConfigInterfaceDirect::~SkFontConfigInterfaceDirect() {}

bool SkFontConfigInterfaceDirect::matchFamilyName(const char familyName[],
                                                  SkFontStyle style,
                                                  FontIdentity* outIdentity,
                                                  SkString* outFamilyName,
                                                  SkFontStyle* outStyle) {
  SkString familyStr(familyName ? familyName : "");

  MatchFontRequest request;
  request.family_name = familyName;
  FontStyleFromSkFontStyle(style, request.style);

  auto status_or_response = GetService<FontManager>().MatchFont(request);
  if (!status_or_response.Ok()) return false;

  if (outIdentity) {
    outIdentity->fTTCIndex = status_or_response->face_index;
    auto font_data = status_or_response->data;
    switch (font_data.type) {
      case FontData::Type::FILE:
        outIdentity->fIsBuffer = false;
        outIdentity->fString = SkString(font_data.path);
        break;
      case FontData::Type::BUFFER:
        outIdentity->fIsBuffer = true;
        outIdentity->fBuffer = font_data.buffer;
        break;
      default:
        std::cout << "FontManager can't handle the FontData type "
                  << (int)font_data.type << std::endl;
        return false;
    }
  }
  if (outFamilyName) {
    *outFamilyName = SkString(status_or_response->family_name);
  }
  if (outStyle) {
    *outStyle = SkFontStyleFromFontStyle(status_or_response->style);
  }
  return true;
}

SkStreamAsset* SkFontConfigInterfaceDirect::openStream(
    const FontIdentity& identity) {
  if (identity.fIsBuffer) {
    return new SkMemoryStream(**identity.fBuffer, identity.fBuffer->GetSize());

  } else {
    return SkStream::MakeFromFile(identity.fString.c_str()).release();
  }
}
