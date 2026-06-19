// Copyright 2024 Google LLC
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

#include <iostream>
#include <string_view>

#include "types.h"

namespace perception {

template <class Derived>
class UniqueIdentifiableType {
 public:
  static size_t TypeId() {
    return reinterpret_cast<size_t>(&type_id_variable__);
  }

  static std::string_view ClassName() {
#if defined(__clang__) || defined(__GNUC__)
    std::string_view pretty(__PRETTY_FUNCTION__);
    size_t begin = pretty.find("Derived = ");
    if (begin != std::string_view::npos) {
      begin += 10;
      size_t end = pretty.find(']', begin);
      if (end != std::string_view::npos) {
        return pretty.substr(begin, end - begin);
      }
    }
    begin = pretty.find("UniqueIdentifiableType<");
    if (begin != std::string_view::npos) {
      begin += 23;
      size_t end = pretty.rfind('>');
      if (end != std::string_view::npos && end > begin) {
        return pretty.substr(begin, end - begin);
      }
    }
    return pretty;
#else
    return "UnknownComponent";
#endif
  }

 private:
  inline static const bool type_id_variable__ = false;
};

}  // namespace perception
