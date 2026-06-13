// Copyright 2026 Google LLC
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

#include <string_view>

#include "perception/serialization/value.h"
#include "perception/service_macros.h"
#include "status.h"

namespace perception {

// Saves a value to the clipboard.
void SetClipboard(const serialization::Value& value);

// Gets a value from the clipboard.
StatusOr<serialization::Value> GetClipboard();

#define METHOD_LIST(X)                           \
  X(1, SetClipboard, void, serialization::Value) \
  X(2, GetClipboard, serialization::Value, void)

DEFINE_PERCEPTION_SERVICE(Clipboard, "perception.Clipboard", METHOD_LIST)
#undef METHOD_LIST

}  // namespace perception
