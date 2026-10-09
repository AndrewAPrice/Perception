// Copyright 2022 Google LLC
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
#include "perception/ui/font.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontConfigInterface.h"
#include "include/ports/SkFontMgr_FontConfigInterface.h"
#include "include/ports/SkFontScanner_FreeType.h"

namespace perception {
namespace ui {
namespace {

SkFontMgr* GetFontManager() {
  static sk_sp<SkFontMgr> font_manager = SkFontMgr_New_FCI(
      sk_sp(SkFontConfigInterface::GetSingletonDirectInterface()),
      SkFontScanner_Make_FreeType());
  return font_manager.get();
}

struct FontKey {
  std::string family_name;
  float size;
  int weight;
  int slant;
  int width;

  bool operator<(const FontKey& other) const {
    if (family_name != other.family_name)
      return family_name < other.family_name;
    if (size != other.size) return size < other.size;
    if (weight != other.weight) return weight < other.weight;
    if (slant != other.slant) return slant < other.slant;
    return width < other.width;
  }
};

bool IsMonospaceFamily(std::string_view fname) {
  return fname.find("mono") != std::string_view::npos ||
         fname.find("monaspace") != std::string_view::npos ||
         fname.find("courier") != std::string_view::npos ||
         fname.find("consolas") != std::string_view::npos ||
         fname.find("menlo") != std::string_view::npos ||
         fname.find("monaco") != std::string_view::npos ||
         fname.find("inconsolata") != std::string_view::npos ||
         fname.find("fira code") != std::string_view::npos ||
         fname.find("source code") != std::string_view::npos ||
         fname.find("lucida console") != std::string_view::npos;
}

bool IsSerifFamily(std::string_view fname) {
  if (fname.find("sans") != std::string_view::npos)
    return false;
  return fname.find("serif") != std::string_view::npos ||
         fname.find("times") != std::string_view::npos ||
         fname.find("georgia") != std::string_view::npos ||
         fname.find("garamond") != std::string_view::npos ||
         fname.find("palatino") != std::string_view::npos ||
         fname.find("cambria") != std::string_view::npos ||
         fname.find("book antiqua") != std::string_view::npos ||
         fname.find("baskerville") != std::string_view::npos ||
         fname.find("century") != std::string_view::npos;
}

std::string CanonicalizeFamily(std::string_view family_name, int width) {
  std::string lower_name(family_name);
  std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });

  bool is_condensed = (width <= SkFontStyle::kSemiCondensed_Width) ||
                      (lower_name.find("condensed") != std::string::npos);

  if (lower_name.find("math") != std::string::npos) return "DejaVuMathTeXGyre";

  if (lower_name.find("monaspaceneon") != std::string::npos ||
      lower_name.find("monaspace neon") != std::string::npos)
    return "MonaspaceNeon";
  if (lower_name.find("monaspaceargon") != std::string::npos ||
      lower_name.find("monaspace argon") != std::string::npos)
    return "MonaspaceArgon";
  if (lower_name.find("monaspacexenon") != std::string::npos ||
      lower_name.find("monaspace xenon") != std::string::npos)
    return "MonaspaceXenon";
  if (lower_name.find("monaspaceradon") != std::string::npos ||
      lower_name.find("monaspace radon") != std::string::npos)
    return "MonaspaceRadon";
  if (lower_name.find("monaspacekrypton") != std::string::npos ||
      lower_name.find("monaspace krypton") != std::string::npos)
    return "MonaspaceKrypton";
  if (lower_name.find("monaspace") != std::string::npos)
    return "MonaspaceNeon";

  if (IsMonospaceFamily(lower_name)) return "DejaVuSansMono";

  if (IsSerifFamily(lower_name))
    return is_condensed ? "DejaVuSerifCondensed" : "DejaVuSerif";

  return is_condensed ? "DejaVuSansCondensed" : "DejaVuSans";
}

}  // namespace

SkFont* GetBook12UiFont() {
  return GetUiFont("DejaVuSans", 12.0f, false, false);
}

SkFont* GetBold12UiFont() {
  return GetUiFont("DejaVuSans", 12.0f, true, false);
}

SkFont* GetMonospace12UiFont() {
  return GetUiFont("DejaVuSansMono", 12.0f, false, false);
}

SkFont* GetMonaspaceUiFont(MonaspaceFamily family, float size, bool bold,
                           bool italic) {
  std::string_view family_name = "MonaspaceNeon";
  switch (family) {
    case MonaspaceFamily::Neon:
      family_name = "MonaspaceNeon";
      break;
    case MonaspaceFamily::Argon:
      family_name = "MonaspaceArgon";
      break;
    case MonaspaceFamily::Xenon:
      family_name = "MonaspaceXenon";
      break;
    case MonaspaceFamily::Radon:
      family_name = "MonaspaceRadon";
      break;
    case MonaspaceFamily::Krypton:
      family_name = "MonaspaceKrypton";
      break;
  }
  return GetUiFont(family_name, size, bold, italic);
}

SkFont* GetUiFont(std::string_view family_name, float size, bool bold,
                  bool italic) {
  return GetUiFont(
      family_name, size,
      bold ? SkFontStyle::kBold_Weight : SkFontStyle::kNormal_Weight,
      italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant,
      SkFontStyle::kNormal_Width);
}

SkFont* GetUiFont(std::string_view family_name, float size, int weight,
                  SkFontStyle::Slant slant, int width) {
  static std::map<FontKey, SkFont*> cached_fonts;

  std::string canonical_family = CanonicalizeFamily(family_name, width);

  FontKey key{canonical_family, size, weight, (int)slant, width};
  auto iterator = cached_fonts.find(key);
  if (iterator != cached_fonts.end()) return iterator->second;

  sk_sp<SkTypeface> typeface = GetFontManager()->matchFamilyStyle(
      canonical_family.c_str(), SkFontStyle(weight, width, slant));
  if (!typeface)
    typeface = GetFontManager()->matchFamilyStyle(
        "DejaVuSans", SkFontStyle(weight, width, slant));

  SkFont* font = new SkFont(typeface, size);

  if (weight >= SkFontStyle::kBold_Weight && font->getTypeface() &&
      !font->getTypeface()->isBold())
    font->setEmbolden(true);
  if (slant != SkFontStyle::kUpright_Slant && font->getTypeface() &&
      !font->getTypeface()->isItalic())
    font->setSkewX(-0.25f);

  cached_fonts[key] = font;
  return font;
}

SkFont* LoadFont(std::string_view path, float size) {
  static std::map<std::string, sk_sp<SkTypeface>> cached_typefaces;
  static std::map<std::pair<std::string, float>, SkFont*> cached_fonts;

  auto key = std::make_pair(std::string(path), size);
  auto it = cached_fonts.find(key);
  if (it != cached_fonts.end()) {
    return it->second;
  }

  std::string path_str = std::string(path);
  sk_sp<SkTypeface> typeface;
  auto tf_it = cached_typefaces.find(path_str);
  if (tf_it != cached_typefaces.end()) {
    typeface = tf_it->second;
  } else {
    typeface = GetFontManager()->makeFromFile(path_str.c_str());
    if (!typeface) return nullptr;
    cached_typefaces[path_str] = typeface;
  }

  SkFont* font = new SkFont(typeface, size);
  cached_fonts[key] = font;
  return font;
}

}  // namespace ui
}  // namespace perception
