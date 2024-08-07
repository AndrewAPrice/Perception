// Copyright 2023 Google LLC
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

#pragma once

#include <memory>
#include <string_view>

#include "include/core/SkImage.h"
#include "modules/svg/include/SkSVGDOM.h"

namespace perception {
namespace ui {

struct Size;

class Image {
 public:
  virtual ~Image();
  static std::shared_ptr<Image> LoadImage(std::string_view path);

  virtual SkImage* GetSkImage(const Size& size, bool& matches_dimensions) = 0;
  virtual SkSVGDOM* GetSkSVGDOM(const Size& size) = 0;
  virtual Size GetSize(const Size& container_size) = 0;
};

}  // namespace ui
}  // namespace perception