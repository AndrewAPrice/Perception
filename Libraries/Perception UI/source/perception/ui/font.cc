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

#include <iostream>
#include <map>
#include <memory>
#include <string>
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

}  // namespace

SkFont* GetBook12UiFont() {
  return GetUiFont("DejaVuSans", 12.0f, false, false);
}

SkFont* GetBold12UiFont() {
  return GetUiFont("DejaVuSans", 12.0f, true, false);
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

  FontKey key{std::string(family_name), size, weight, (int)slant, width};
  auto it = cached_fonts.find(key);
  if (it != cached_fonts.end()) return it->second;

  std::string family_str =
      family_name.empty() ? "DejaVuSans" : std::string(family_name);

  SkFont* font =
      new SkFont(GetFontManager()->matchFamilyStyle(
                     family_str.c_str(), SkFontStyle(weight, width, slant)),
                 size);

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
