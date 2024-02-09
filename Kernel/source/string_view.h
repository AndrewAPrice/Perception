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

#include "types.h"

// A wrapper around a non-null terminated string. StringView does not own the
// string, so the underlying data needs to stay in scope.
class StringView {
 public:
  // Constructs a string view around "str" and "length".
  StringView(const char *str, size_t length) : str(str), length(length) {}

  // The source string.
  const char *str;

  // The length of the string.
  size_t length;
};
