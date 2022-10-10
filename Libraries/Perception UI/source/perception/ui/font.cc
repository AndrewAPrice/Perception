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

namespace perception {
namespace ui {
namespace {

std::unique_ptr<SkFont> ui_font;

}

SkFont* GetUiFont() {
  if (!ui_font) {
    ui_font = std::make_unique<SkFont>(nullptr, 16);
  }
  return ui_font.get();
}

}  // namespace ui
}  // namespace perception