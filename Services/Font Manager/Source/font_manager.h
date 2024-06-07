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

#include "permebuf/Libraries/perception/font_manager.permebuf.h"

#include <mutex>
#include "fontconfig/fontconfig.h"

class FontManager : public ::permebuf::perception::FontManager::Server {
 public:
  typedef ::permebuf::perception::FontManager FM;

  FontManager();
  ~FontManager();

  virtual StatusOr<Permebuf<FM::MatchFontResponse>> HandleMatchFont(
      ::perception::ProcessId sender,
      Permebuf<FM::MatchFontRequest> request) override;

  virtual StatusOr<Permebuf<FM::GetFontFamiliesResponse>> HandleGetFontFamilies(
      ::perception::ProcessId sender,
      const FM::GetFontFamiliesRequest& request) override;

  virtual StatusOr<Permebuf<FM::GetFontFamilyStylesResponse>> HandleGetFontFamilyStyles(
      ::perception::ProcessId sender,
      Permebuf<FM::GetFontFamilyStylesRequest> request) override;

  private:
    FcConfig* config_;
    std::mutex mutex_;
};