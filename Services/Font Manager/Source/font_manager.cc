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

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>

#include "perception/memory_mapped_file.h"
#include "perception/services.h"
#include "perception/shared_memory.h"
#include "perception/storage_manager.h"
#include "status.h"

using ::perception::GetService;
using ::perception::MemoryMappedFile;
using ::perception::SharedMemory;
using ::perception::Status;
using ::perception::StorageManager;
using ::perception::ui::FontData;
using ::perception::ui::FontFamilies;
using ::perception::ui::FontFamily;
using ::perception::ui::FontStyle;
using ::perception::ui::FontStyles;
using ::perception::ui::MatchFontRequest;
using ::perception::ui::MatchFontResponse;

namespace {

struct MemoryMappedFont {
  ::perception::MemoryMappedFile::Client file;
  std::shared_ptr<SharedMemory> buffer;
};

std::map<std::string, std::shared_ptr<MemoryMappedFont>> font_data_by_path;

Status MakeSureFontIsLoaded(const std::string& path) {
  if (!font_data_by_path.contains(path)) {
    // Open the font as a memory mapped file.
    ASSIGN_OR_RETURN(auto response,
                     GetService<StorageManager>().OpenMemoryMappedFile({path}));

    auto memory_mapped_font = std::make_shared<MemoryMappedFont>();
    memory_mapped_font->file = response.file;
    memory_mapped_font->buffer = response.file_contents;

    font_data_by_path[path] = memory_mapped_font;
  }
  return ::perception::Status::OK;
}

FontStyle MakeStyle(FontStyle::Weight weight, FontStyle::Width width,
                    FontStyle::Slant slant) {
  FontStyle style;
  style.weight = weight;
  style.width = width;
  style.slant = slant;
  return style;
}

}  // namespace

FontManager::FontManager() {}

FontManager::~FontManager() {}

StatusOr<MatchFontResponse> FontManager::MatchFont(
    const MatchFontRequest& request) {
  std::scoped_lock lock(mutex_);

  std::string fname = request.family_name;
  std::transform(fname.begin(), fname.end(), fname.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  bool is_condensed =
      (request.style.width <= FontStyle::Width::SEMICONDENSED) ||
      (fname.find("condensed") != std::string::npos);
  bool is_bold =
      ((int)request.style.weight >= (int)FontStyle::Weight::SEMIBOLD);
  bool is_italic = (request.style.slant == FontStyle::Slant::ITALIC ||
                    request.style.slant == FontStyle::Slant::OBLIQUE);
  bool is_extralight =
      ((int)request.style.weight <= (int)FontStyle::Weight::EXTRALIGHT);

  std::string font_path;
  std::string resolved_family;

  if (fname.find("math") != std::string::npos) {
    font_path = "/Libraries/Fonts/DejaVuMathTeXGyre.ttf";
    resolved_family = "DejaVuMathTeXGyre";
  } else if (fname.find("mono") != std::string::npos ||
             fname.find("courier") != std::string::npos ||
             fname.find("consolas") != std::string::npos) {
    resolved_family = "DejaVuSansMono";
    if (is_bold && is_italic) {
      font_path = "/Libraries/Fonts/DejaVuSansMono-BoldOblique.ttf";
    } else if (is_bold) {
      font_path = "/Libraries/Fonts/DejaVuSansMono-Bold.ttf";
    } else if (is_italic) {
      font_path = "/Libraries/Fonts/DejaVuSansMono-Oblique.ttf";
    } else {
      font_path = "/Libraries/Fonts/DejaVuSansMono.ttf";
    }
  } else if (fname.find("serif") != std::string::npos &&
             fname.find("sans") == std::string::npos) {
    if (is_condensed) {
      resolved_family = "DejaVuSerifCondensed";
      if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-BoldItalic.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-Italic.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed.ttf";
      }
    } else {
      resolved_family = "DejaVuSerif";
      if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerif-BoldItalic.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSerif-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerif-Italic.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSerif.ttf";
      }
    }
  } else if (fname.find("times") != std::string::npos ||
             fname.find("georgia") != std::string::npos ||
             fname.find("garamond") != std::string::npos) {
    if (is_condensed) {
      resolved_family = "DejaVuSerifCondensed";
      if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-BoldItalic.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed-Italic.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSerifCondensed.ttf";
      }
    } else {
      resolved_family = "DejaVuSerif";
      if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerif-BoldItalic.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSerif-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSerif-Italic.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSerif.ttf";
      }
    }
  } else {
    // Default is Sans
    if (is_condensed) {
      resolved_family = "DejaVuSansCondensed";
      if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSansCondensed-BoldOblique.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSansCondensed-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSansCondensed-Oblique.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSansCondensed.ttf";
      }
    } else {
      resolved_family = "DejaVuSans";
      if (is_extralight && !is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSans-ExtraLight.ttf";
      } else if (is_bold && is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSans-BoldOblique.ttf";
      } else if (is_bold) {
        font_path = "/Libraries/Fonts/DejaVuSans-Bold.ttf";
      } else if (is_italic) {
        font_path = "/Libraries/Fonts/DejaVuSans-Oblique.ttf";
      } else {
        font_path = "/Libraries/Fonts/DejaVuSans.ttf";
      }
    }
  }

  if (MakeSureFontIsLoaded(font_path) != ::perception::Status::OK) {
    font_path = "/Libraries/Fonts/DejaVuSans.ttf";
    RETURN_ON_ERROR(MakeSureFontIsLoaded(font_path));
  }

  MatchFontResponse response;
  response.face_index = 0;
  response.family_name =
      request.family_name.empty() ? resolved_family : request.family_name;
  response.data.type = FontData::Type::BUFFER;
  response.data.buffer = font_data_by_path[font_path]->buffer;
  response.style.weight = is_bold
                              ? FontStyle::Weight::BOLD
                              : (is_extralight ? FontStyle::Weight::EXTRALIGHT
                                               : FontStyle::Weight::REGULAR);
  response.style.width =
      is_condensed ? FontStyle::Width::CONDENSED : FontStyle::Width::NORMAL;
  response.style.slant =
      is_italic ? FontStyle::Slant::ITALIC : FontStyle::Slant::UPRIGHT;
  return response;
}

StatusOr<FontFamilies> FontManager::GetFontFamilies() {
  std::scoped_lock lock(mutex_);
  FontFamilies response;
  for (const auto& name : {
           "DejaVuMathTeXGyre",
           "DejaVuSansMono",
           "DejaVuSerifCondensed",
           "DejaVuSerif",
           "DejaVuSansCondensed",
           "DejaVuSans",
       }) {
    FontFamily family;
    family.name = name;
    response.families.push_back(family);
  }
  return response;
}

StatusOr<FontStyles> FontManager::GetFontFamilyStyles(
    const ::perception::ui::FontFamily& request) {
  std::scoped_lock lock(mutex_);

  std::string fname = request.name;
  std::transform(fname.begin(), fname.end(), fname.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  bool is_condensed = fname.find("condensed") != std::string::npos;

  FontStyles response;

  if (fname.find("math") != std::string::npos) {
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR,
                                        FontStyle::Width::NORMAL,
                                        FontStyle::Slant::UPRIGHT));
  } else if (fname.find("mono") != std::string::npos ||
             fname.find("courier") != std::string::npos ||
             fname.find("consolas") != std::string::npos) {
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR,
                                        FontStyle::Width::NORMAL,
                                        FontStyle::Slant::UPRIGHT));
    response.styles.push_back(MakeStyle(FontStyle::Weight::BOLD,
                                        FontStyle::Width::NORMAL,
                                        FontStyle::Slant::UPRIGHT));
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR,
                                        FontStyle::Width::NORMAL,
                                        FontStyle::Slant::ITALIC));
    response.styles.push_back(MakeStyle(FontStyle::Weight::BOLD,
                                        FontStyle::Width::NORMAL,
                                        FontStyle::Slant::ITALIC));
  } else if (fname.find("serif") != std::string::npos &&
             fname.find("sans") == std::string::npos) {
    FontStyle::Width width =
        is_condensed ? FontStyle::Width::CONDENSED : FontStyle::Width::NORMAL;
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR, width,
                                        FontStyle::Slant::UPRIGHT));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::UPRIGHT));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::REGULAR, width, FontStyle::Slant::ITALIC));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::ITALIC));
  } else if (fname.find("times") != std::string::npos ||
             fname.find("georgia") != std::string::npos ||
             fname.find("garamond") != std::string::npos) {
    FontStyle::Width width =
        is_condensed ? FontStyle::Width::CONDENSED : FontStyle::Width::NORMAL;
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR, width,
                                        FontStyle::Slant::UPRIGHT));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::UPRIGHT));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::REGULAR, width, FontStyle::Slant::ITALIC));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::ITALIC));
  } else {
    FontStyle::Width width =
        is_condensed ? FontStyle::Width::CONDENSED : FontStyle::Width::NORMAL;
    response.styles.push_back(MakeStyle(FontStyle::Weight::REGULAR, width,
                                        FontStyle::Slant::UPRIGHT));
    if (!is_condensed) {
      response.styles.push_back(MakeStyle(FontStyle::Weight::EXTRALIGHT, width,
                                          FontStyle::Slant::UPRIGHT));
    }
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::UPRIGHT));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::REGULAR, width, FontStyle::Slant::ITALIC));
    response.styles.push_back(
        MakeStyle(FontStyle::Weight::BOLD, width, FontStyle::Slant::ITALIC));
  }

  return response;
}
