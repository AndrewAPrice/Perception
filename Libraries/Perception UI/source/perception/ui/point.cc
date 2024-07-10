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

#include "perception/ui/point.h"

#include "perception/ui/size.h"

namespace perception {
namespace ui {

bool Point::operator==(const Point& other) const {
  return x == other.x && y == other.y;
}

Point Point::operator+(const Point& other) const {
  return {x + other.x, y + other.y};
}

Point& Point::operator+=(const Point& other) {
  x += other.x;
  y += other.y;
  return *this;
}

Point Point::operator-(const Point& other) const {
  return {x - other.x, y - other.y};
}

Point& Point::operator-=(const Point& other) {
  x -= other.x;
  y -= other.y;
  return *this;
}

Size Point::ToSize() const { return {.width = x, .height = y}; }

}  // namespace ui
}  // namespace perception
