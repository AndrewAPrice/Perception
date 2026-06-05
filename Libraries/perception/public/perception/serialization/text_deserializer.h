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

#include <types.h>

#include <string_view>

namespace perception {
namespace serialization {

class Serializable;

// Deserializes a serializable object from its human readable text format.
bool DeserializeFromString(Serializable& object, std::string_view text);

}  // namespace serialization
}  // namespace perception
