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

#include "perception/ui/rectangle.h"

#include <algorithm>

#include "perception/ui/point.h"
#include "perception/ui/size.h"

namespace perception {
namespace ui {

Rectangle Rectangle::FromMinMaxPoints(const Point& min, const Point& max) {
  return Rectangle{.origin = min, .size = (max - min).ToSize()};
}

bool Rectangle::operator==(const Rectangle& other) const {
  return origin == other.origin && size == other.size;
}

float Rectangle::MinX() const { return origin.x; }
float Rectangle::MinY() const { return origin.y; }
float Rectangle::MaxX() const { return origin.x + size.width; }
float Rectangle::MaxY() const { return origin.y + size.height; }
float Rectangle::Width() const { return size.width; }
float Rectangle::Height() const { return size.height; }

Point Rectangle::Min() const { return origin; }
Point Rectangle::Max() const { return {.x = MaxX(), .y = MaxY()}; }

Rectangle Rectangle::Intersection(const Rectangle& other) const {
  Point min = {.x = std::max(MinX(), other.MinX()),
               .y = std::max(MinY(), other.MinY())};
  Point max = {.x = std::min(MaxX(), other.MaxX()),
               .y = std::min(MaxY(), other.MaxY())};
  Size size = (max - min).ToSize();
  return {.origin = min, .size = size};
}

Rectangle Rectangle::Union(const Rectangle& other) const {
  Point min = {.x = std::min(MinX(), other.MinX()),
               .y = std::min(MinY(), other.MinY())};
  Point max = {.x = std::max(MaxX(), other.MaxX()),
               .y = std::max(MaxY(), other.MaxY())};
  return FromMinMaxPoints(min, max);
}

bool Rectangle::Intersects(const Rectangle& other) const {
  if (MaxX() < other.MinX() || MaxY() < other.MinY() || MinX() > other.MaxX() ||
      MinY() > other.MaxY()) {
    return false;
  }
  return true;
}

bool Rectangle::Contains(const Rectangle& child) const {
  if (child.MinX() < MinX() || child.MinY() < MinY() ||
      child.MaxX() >= MaxX() || child.MaxY() >= MaxY()) {
    return false;
  }
  return true;
}

bool Rectangle::Contains(const Point& point) const {
  if (point.x < MinX() || point.y < MinY() || point.x >= MaxX() ||
      point.y >= MaxY()) {
    return false;
  }
  return true;
}

}  // namespace ui
}  // namespace perception
