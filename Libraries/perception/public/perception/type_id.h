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

#include "types.h"

namespace perception {
namespace internal {

size_t NextUniqueId() noexcept;

template <typename Type, typename = void>
struct TypeId final {
  [[nodiscard]] static size_t operator()() noexcept {
    static const size_t value = NextUniqueId();
    return value;
  }
};

}  // namespace internal

// Returns a unique ID given a type.
template <typename Type, typename = void>
constexpr size_t TypeId() {
    return internal::TypeId<Type>()();
};


}  // namespace perception
