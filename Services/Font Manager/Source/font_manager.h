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

#include <mutex>

#include "fontconfig/fontconfig.h"
#include "perception/ui/font_manager.h"

class FontManager : public ::perception::ui::FontManager::Server {
 public:
  FontManager();
  ~FontManager();

  virtual StatusOr<::perception::ui::MatchFontResponse> MatchFont(
      const ::perception::ui::MatchFontRequest& request) override;

  virtual StatusOr<::perception::ui::FontFamilies> GetFontFamilies() override;

  virtual StatusOr<::perception::ui::FontStyles> GetFontFamilyStyles(
      const ::perception::ui::FontFamily& request) override;

 private:
  FcConfig* config_;
  std::mutex mutex_;
};