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

// Creates operators so enums can be | and & together.
#define ENUM_BINARY_OPERATORS(EnumType)                                        \
  inline EnumType operator|(EnumType a, EnumType b) {                          \
    return static_cast<EnumType>(static_cast<int>(a) | static_cast<int>(b));   \
  }                                                                            \
                                                                               \
  inline bool operator&(EnumType a, EnumType b) {                              \
    return (static_cast<int>(a) & static_cast<int>(b)) == static_cast<int>(b); \
  }
