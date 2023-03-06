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

#include "font_manager.h"

#include <map>

#include "perception/shared_memory.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

using ::perception::SharedMemory;
using ::perception::Status;
using ::permebuf::perception::FontData;
using ::permebuf::perception::MemoryMappedFile;
using ::permebuf::perception::StorageManager;
using FM = ::permebuf::perception::FontManager;
using FontStyle = ::permebuf::perception::FontStyle;

namespace {

auto kFontWeightToFcInt = std::map<FontStyle::Weight, int>(
    {{FontStyle::Weight::THIN, FC_WEIGHT_THIN},
     {FontStyle::Weight::EXTRALIGHT, FC_WEIGHT_EXTRALIGHT},
     {FontStyle::Weight::LIGHT, FC_WEIGHT_LIGHT},
     {FontStyle::Weight::SEMILIGHT, FC_WEIGHT_DEMILIGHT},
     {FontStyle::Weight::BOOK, FC_WEIGHT_BOOK},
     {FontStyle::Weight::REGULAR, FC_WEIGHT_REGULAR},
     {FontStyle::Weight::MEDIUM, FC_WEIGHT_MEDIUM},
     {FontStyle::Weight::SEMIBOLD, FC_WEIGHT_DEMIBOLD},
     {FontStyle::Weight::BOLD, FC_WEIGHT_BOLD},
     {FontStyle::Weight::EXTRABOLD, FC_WEIGHT_EXTRABOLD},
     {FontStyle::Weight::BLACK, FC_WEIGHT_BLACK},
     {FontStyle::Weight::EXTRABLACK, FC_WEIGHT_EXTRABLACK}});

auto kFontWidthToFcInt = std::map<FontStyle::Width, int>(
    {{FontStyle::Width::ULTRACONDENSED, FC_WIDTH_ULTRACONDENSED},
     {FontStyle::Width::EXTRACONDENSED, FC_WIDTH_EXTRACONDENSED},
     {FontStyle::Width::CONDENSED, FC_WIDTH_CONDENSED},
     {FontStyle::Width::SEMICONDENSED, FC_WIDTH_SEMICONDENSED},
     {FontStyle::Width::NORMAL, FC_WIDTH_NORMAL},
     {FontStyle::Width::SEMIEXPANDED, FC_WIDTH_SEMIEXPANDED},
     {FontStyle::Width::EXPANDED, FC_WIDTH_EXPANDED},
     {FontStyle::Width::EXTRAEXPANDED, FC_WIDTH_EXTRAEXPANDED},
     {FontStyle::Width::ULTRAEXPANDED, FC_WIDTH_ULTRAEXPANDED}});

auto kFontSlantToFcInt = std::map<FontStyle::Slant, int>(
    {{FontStyle::Slant::UPRIGHT, FC_SLANT_ROMAN},
     {FontStyle::Slant::ITALIC, FC_SLANT_ITALIC},
     {FontStyle::Slant::OBLIQUE, FC_SLANT_OBLIQUE}});

auto kFcIntToFontWeight = std::map<int, FontStyle::Weight>(
    {{FC_WEIGHT_THIN, FontStyle::Weight::THIN},
     {FC_WEIGHT_EXTRALIGHT, FontStyle::Weight::EXTRALIGHT},
     {FC_WEIGHT_LIGHT, FontStyle::Weight::LIGHT},
     {FC_WEIGHT_DEMILIGHT, FontStyle::Weight::SEMILIGHT},
     {FC_WEIGHT_BOOK, FontStyle::Weight::BOOK},
     {FC_WEIGHT_REGULAR, FontStyle::Weight::REGULAR},
     {FC_WEIGHT_MEDIUM, FontStyle::Weight::MEDIUM},
     {FC_WEIGHT_DEMIBOLD, FontStyle::Weight::SEMIBOLD},
     {FC_WEIGHT_BOLD, FontStyle::Weight::BOLD},
     {FC_WEIGHT_EXTRABOLD, FontStyle::Weight::EXTRABOLD},
     {FC_WEIGHT_BLACK, FontStyle::Weight::BLACK},
     {FC_WEIGHT_EXTRABLACK, FontStyle::Weight::EXTRABLACK}});

auto kFcIntToFontWidth = std::map<int, FontStyle::Width>(
    {{FC_WIDTH_ULTRACONDENSED, FontStyle::Width::ULTRACONDENSED},
     {FC_WIDTH_EXTRACONDENSED, FontStyle::Width::EXTRACONDENSED},
     {FC_WIDTH_CONDENSED, FontStyle::Width::CONDENSED},
     {FC_WIDTH_SEMICONDENSED, FontStyle::Width::SEMICONDENSED},
     {FC_WIDTH_NORMAL, FontStyle::Width::NORMAL},
     {FC_WIDTH_SEMIEXPANDED, FontStyle::Width::SEMIEXPANDED},
     {FC_WIDTH_EXPANDED, FontStyle::Width::EXPANDED},
     {FC_WIDTH_EXTRAEXPANDED, FontStyle::Width::EXTRAEXPANDED},
     {FC_WIDTH_ULTRAEXPANDED, FontStyle::Width::ULTRAEXPANDED}});

auto kFcIntToFontSlant = std::map<int, FontStyle::Slant>(
    {{FC_SLANT_ROMAN, FontStyle::Slant::UPRIGHT},
     {FC_SLANT_ITALIC, FontStyle::Slant::ITALIC},
     {FC_SLANT_OBLIQUE, FontStyle::Slant::OBLIQUE}});

struct MemoryMappedFont {
  MemoryMappedFile file;
  SharedMemory buffer;
};

std::map<std::string, std::shared_ptr<MemoryMappedFont>> font_data_by_path;

const char* GetString(FcPattern* pattern, const char field[], int index = 0) {
  const char* name;
  if (FcPatternGetString(pattern, field, index, (FcChar8**)&name) !=
      FcResultMatch) {
    name = nullptr;
  }
  return name;
}

int GetInt(FcPattern& pattern, const char object[], int missing) {
  int value;
  if (FcPatternGetInteger(&pattern, object, 0, &value) != FcResultMatch) {
    return missing;
  }
  return value;
}

template <typename K, typename V>
V GetOrDefault(const std::map<K, V>& m, const K key, const V default_value) {
  typename std::map<K, V>::const_iterator it = m.find(key);
  return it == m.end() ? default_value : it->second;
}

void PopulateFcPatternFromFontStyle(const FontStyle& style,
                                    FcPattern& pattern) {
  FcPatternAddInteger(
      &pattern, FC_WEIGHT,
      GetOrDefault(kFontWeightToFcInt, style.GetWeight(), FC_WEIGHT_REGULAR));
  FcPatternAddInteger(
      &pattern, FC_WIDTH,
      GetOrDefault(kFontWidthToFcInt, style.GetWidth(), FC_WIDTH_NORMAL));
  FcPatternAddInteger(
      &pattern, FC_SLANT,
      GetOrDefault(kFontSlantToFcInt, style.GetSlant(), FC_SLANT_ROMAN));
}

void PopulateFontStyleFromFcPattern(FcPattern& pattern, FontStyle style) {
  style.SetWeight(GetOrDefault(kFcIntToFontWeight,
                               GetInt(pattern, FC_WEIGHT, FC_WEIGHT_REGULAR),
                               FontStyle::Weight::REGULAR));
  style.SetWidth(GetOrDefault(kFcIntToFontWidth,
                              GetInt(pattern, FC_WIDTH, FC_WIDTH_NORMAL),
                              FontStyle::Width::NORMAL));
  style.SetSlant(GetOrDefault(kFcIntToFontSlant,
                              GetInt(pattern, FC_SLANT, FC_SLANT_ROMAN),
                              FontStyle::Slant::UPRIGHT));
}

Status MakeSureFontIsLoaded(const std::string& path) {
  if (!font_data_by_path.contains(path)) {
    // Open the font as a memory mapped file.
    Permebuf<StorageManager::OpenMemoryMappedFileRequest> request;
    request->SetPath(path);

    ASSIGN_OR_RETURN(
        auto response,
        StorageManager::Get().CallOpenMemoryMappedFile(std::move(request)));

    auto memory_mapped_font = std::make_shared<MemoryMappedFont>();
    memory_mapped_font->file = response.GetFile();
    memory_mapped_font->buffer = response.GetFileContents().Clone();

    font_data_by_path[path] = memory_mapped_font;
  }
  return ::perception::Status::OK;
}

}  // namespace

FontManager::FontManager() {
  config_ = FcConfigReference(nullptr);
}

FontManager::~FontManager() { FcConfigDestroy(config_); }

StatusOr<Permebuf<FM::MatchFontResponse>> FontManager::HandleMatchFont(
    ::perception::ProcessId sender, Permebuf<FM::MatchFontRequest> request) {
  std::scoped_lock lock(mutex_);

  // Most of this is copied from SkFontConfigInterface_direct.cpp from Skia
  // then customized for Perception.
  FcPattern* pattern = FcPatternCreate();
  std::string_view family_name = *request->GetFamilyName();
  if (!request->GetFamilyName().IsEmpty()) {
    FcPatternAddString(pattern, FC_FAMILY,
                       (FcChar8*)request->GetFamilyName().RawString());
  }

  PopulateFcPatternFromFontStyle(request->GetStyle(), *pattern);

  FcPatternAddBool(pattern, FC_SCALABLE, FcTrue);
  FcConfigSubstitute(config_, pattern, FcMatchPattern);
  FcDefaultSubstitute(pattern);

  const char* post_config_family = GetString(pattern, FC_FAMILY);
  if (!post_config_family) {
    // we can just continue with an empty name, e.g. default font
    post_config_family = "";
  }

  FcResult result;
  FcFontSet* font_set = FcFontSort(config_, pattern, 0, nullptr, &result);

  if (!font_set || font_set->nfont == 0) {
    FcPatternDestroy(pattern);
    return ::perception::Status::INTERNAL_ERROR;
  }

  FcPattern* match = font_set->fonts[0];
  if (!match) {
    FcPatternDestroy(pattern);
    FcFontSetDestroy(font_set);
    return ::perception::Status::INTERNAL_ERROR;
  }

  post_config_family = GetString(match, FC_FAMILY);
  if (!post_config_family) {
    FcFontSetDestroy(font_set);
    return perception::Status::INTERNAL_ERROR;
  }

  const char* c_filename = GetString(match, FC_FILE);
  if (!c_filename) {
    FcFontSetDestroy(font_set);
    return perception::Status::INTERNAL_ERROR;
  }

  const char* sysroot = (const char*)FcConfigGetSysRoot(config_);
  std::string resolved_filename;
  if (sysroot) {
    resolved_filename = sysroot;
  }
  resolved_filename += c_filename;

  int face_index = GetInt(*match, FC_INDEX, 0);

  FcFontSetDestroy(font_set);

  RETURN_ON_ERROR(MakeSureFontIsLoaded(resolved_filename));

  auto response = Permebuf<FM::MatchFontResponse>();

  response->SetFaceIndex(face_index);
  response->SetFamilyName(std::string_view(post_config_family));
  response->MutableData().MutableBuffer().SetBuffer(
      font_data_by_path[resolved_filename]->buffer);

  PopulateFontStyleFromFcPattern(*match, response->MutableStyle());

  return response;
}

StatusOr<Permebuf<FM::GetFontFamiliesResponse>>
FontManager::HandleGetFontFamilies(::perception::ProcessId sender,
                                   const FM::GetFontFamiliesRequest& request) {
  std::scoped_lock(mutex_);
  std::cout << "TODO: Implement FontManager::HandleGetFontFamilies"
            << std::endl;
  return Permebuf<FM::GetFontFamiliesResponse>();
}

StatusOr<Permebuf<FM::GetFontFamilyStylesResponse>>
FontManager::HandleGetFontFamilyStyles(
    ::perception::ProcessId sender,
    Permebuf<FM::GetFontFamilyStylesRequest> request) {
  std::scoped_lock(mutex_);
  std::cout << "TODO: Implement FontManager::HandleGetFontFamilyStyles"
            << std::endl;
  return Permebuf<FM::GetFontFamilyStylesResponse>();
}