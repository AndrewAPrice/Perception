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

#include <memory>

#include "include/core/SkFont.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontConfigInterface.h"
#include "include/ports/SkFontMgr_FontConfigInterface.h"
#include "include/ports/SkFontConfigInterface.h"

namespace perception {
namespace ui {
namespace {

std::unique_ptr<SkFont> book_12_ui_font;
std::unique_ptr<SkFont> bold_12_ui_font;

SkFontMgr *GetFontManager() {
  static sk_sp<SkFontMgr> font_manager = SkFontMgr_New_FCI(
    sk_sp( SkFontConfigInterface::GetSingletonDirectInterface()));
  return font_manager.get();
}

}

SkFont* GetBook12UiFont() {
  if (!book_12_ui_font) {
    book_12_ui_font = std::make_unique<SkFont>(
        GetFontManager()->legacyMakeTypeface(
            "DejaVuSans",
            SkFontStyle(SkFontStyle::kNormal_Weight, SkFontStyle::kNormal_Width,
                        SkFontStyle::kUpright_Slant)),
        12);
  }
  return book_12_ui_font.get();
}

SkFont* GetBold12UiFont() {
  if (!bold_12_ui_font) {
    bold_12_ui_font = std::make_unique<SkFont>(
        GetFontManager()->legacyMakeTypeface(
            "DejaVuSans",
            SkFontStyle(SkFontStyle::kBold_Weight, SkFontStyle::kNormal_Width,
                        SkFontStyle::kUpright_Slant)),
        12);

  }
  return bold_12_ui_font.get();
}

}  // namespace ui
}  // namespace perception