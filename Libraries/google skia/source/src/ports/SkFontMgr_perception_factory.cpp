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

#include "include/core/SkFontMgr.h"
#include "include/ports/SkFontMgr_directory.h"

#ifndef SK_FONT_FILE_PREFIX
#  if defined(SK_BUILD_FOR_MAC) || defined(SK_BUILD_FOR_IOS)
#    define SK_FONT_FILE_PREFIX "/System/Library/Fonts/"
#  else
#    define SK_FONT_FILE_PREFIX "/usr/share/fonts/"
#  endif
#endif

sk_sp<SkFontMgr> SkFontMgr::Factory() {
    return SkFontMgr_New_Custom_Directory("/Libraries/Fonts/assets/");
}
