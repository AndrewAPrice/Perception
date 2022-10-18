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
#include "include/private/SkFixed.h"
#include "include/private/SkMutex.h"
#include "include/private/SkTArray.h"
#include "include/private/SkTDArray.h"
#include "include/private/SkTemplates.h"
#include "permebuf/Libraries/perception/font_manager.permebuf.h"
#include "src/core/SkAutoMalloc.h"
#include "src/core/SkBuffer.h"

using ::permebuf::perception::FontManager;
using FontStyle = ::permebuf::perception::FontStyle;
using SkFS = SkFontStyle;

namespace {
auto kFontWeightToSkiaWeight = std::map<FontStyle::Weight, int>({
    {FontStyle::Weight::THIN, SkFS::kThin_Weight},
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
    {FontStyle::Weight::EXTRABLACK, SkFS::kExtraBlack_Weight}
});

auto kFontWidthToSkiaWidth = std::map<FontStyle::Width, SkFS::Width>({
    {FontStyle::Width::ULTRACONDENSED, SkFS::kUltraCondensed_Width},
    {FontStyle::Width::EXTRACONDENSED, SkFS::kExtraCondensed_Width},
    {FontStyle::Width::CONDENSED, SkFS::kCondensed_Width},
    {FontStyle::Width::SEMICONDENSED, SkFS::kSemiCondensed_Width},
    {FontStyle::Width::NORMAL, SkFS::kNormal_Width},
    {FontStyle::Width::SEMIEXPANDED, SkFS::kSemiExpanded_Width},
    {FontStyle::Width::EXPANDED, SkFS::kExpanded_Width},
    {FontStyle::Width::EXTRAEXPANDED, SkFS::kExtraExpanded_Width},
    {FontStyle::Width::ULTRAEXPANDED, SkFS::kUltraExpanded_Width}
});

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

SkFontStyle SkFontStyleFromFontStyle(FontStyle font_style) {
  typedef SkFontStyle SkFS;

  int weight = (int)GetOrDefault(kFontWeightToSkiaWeight, font_style.GetWeight(),
                            (int)SkFS::kNormal_Weight);
  int width = (int)GetOrDefault(kFontWidthToSkiaWidth, font_style.GetWidth(),
                           SkFS::kNormal_Width);
  SkFS::Slant slant = GetOrDefault(kFontSlantToSkiaSlant, font_style.GetSlant(),
                           SkFS::kUpright_Slant);

  return SkFontStyle(weight, width, slant);
}

void FontStyleFromSkFontStyle(SkFontStyle style, FontStyle font_style) {
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
  font_style.SetWeight((FontStyle::Weight)SkScalarRoundToInt(
      map_ranges(style.weight(), weight_ranges, std::size(weight_ranges))));

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
  font_style.SetWidth((FontStyle::Width)SkScalarRoundToInt(
      map_ranges(style.width(), width_ranges, std::size(width_ranges))));

  switch (style.slant()) {
    case SkFS::kUpright_Slant:
      font_style.SetSlant(FontStyle::Slant::UPRIGHT);
      break;
    case SkFS::kItalic_Slant:
      font_style.SetSlant(FontStyle::Slant::ITALIC);
      break;
    case SkFS::kOblique_Slant:
      font_style.SetSlant(FontStyle::Slant::OBLIQUE);
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

  Permebuf<FontManager::MatchFontRequest> request;
  request->SetFamilyName(familyName);
  FontStyleFromSkFontStyle(style, request->MutableStyle());

  auto status_or_response =
      FontManager::Get().CallMatchFont(std::move(request));
  if (!status_or_response.Ok()) return false;

  if (outIdentity) {
    outIdentity->fTTCIndex = ((*status_or_response)->GetFaceIndex());
    outIdentity->fString = SkString(*(*status_or_response)->GetPath());
  }
  if (outFamilyName) {
    *outFamilyName = SkString(*(*status_or_response)->GetFamilyName());
  }
  if (outStyle) {
    *outStyle = SkFontStyleFromFontStyle((*status_or_response)->GetStyle());
  }
  return true;
}

SkStreamAsset* SkFontConfigInterfaceDirect::openStream(
    const FontIdentity& identity) {
  return SkStream::MakeFromFile(identity.fString.c_str()).release();
}
