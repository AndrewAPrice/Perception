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

#include "perception/serialization/serializable.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace window {

// Represents a rectangle.
struct Rectangle : public serialization::Serializable {
  Rectangle() : min_x(0), min_y(0), max_x(0), max_y(0) {}
  Rectangle(int min_x, int min_y, int max_x, int max_y)
      : min_x(min_x), min_y(min_y), max_x(max_x), max_y(max_y) {}

  // Minimum X coordinate, inclusive.
  int min_x;

  // Minimum Y coordinate, inclusive.
  int min_y;

  // Maximum X coordinate, exclusive.
  int max_x;

  // Maximum Y coordinate, exclusive.
  int max_y;

  bool operator==(const Rectangle& other) const {
    return min_x == other.min_x && min_y == other.min_y &&
           max_x == other.max_x && max_y == other.max_y;
  }
  bool operator!=(const Rectangle& other) const { return !(*this == other); }

  virtual void Serialize(serialization::Serializer& serializer) override;
};

}  // namespace window
}  // namespace perception