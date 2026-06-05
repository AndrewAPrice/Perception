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

#include <string>
#include <string_view>

#include "perception/serialization/value.h"
#include "status.h"

namespace perception {
namespace serialization {

// Converts a simplified, keyless JSON-like string representation into a Value.
//
// Unlike normal perception type serialization (which serializes variables with
// their names/metadata, e.g. using `text_serializer`), this format is compact,
// keyless, and suitable for configuration scripts, settings files, or CSV-like
// structures.
//
// Examples of inputs parsed:
// - null
// - true / false
// - 123456 / -789
// - 3.14159 / 0.0
// - "Hello World"
// - [1, "two", 3.3] (with optional trailing commas)
StatusOr<Value> StringToValue(std::string_view str);

// Converts a Value into its simplified, keyless JSON-like string
// representation.
//
// Unlike the standard type serialization (which yields key-value records like
// `{bool_value: 1}`), this function formats the Value directly into a short,
// human-readable format.
//
// Examples of output:
// - Value() -> "null"
// - Value(true) -> "true"
// - Value(123456) -> "123456"
// - Value(3.14) -> "3.14"
// - Value("Hello") -> "\"Hello\"" (special characters escaped)
// - Value(vector) -> "[1, \"two\", 3.3]"
std::string ValueToString(const Value& value);

}  // namespace serialization
}  // namespace perception
