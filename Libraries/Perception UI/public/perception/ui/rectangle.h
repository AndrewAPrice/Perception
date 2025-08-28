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

#include <algorithm>

#include "perception/ui/point.h"
#include "perception/ui/size.h"

namespace perception {
namespace ui {

struct Rectangle {
  Point origin;
  Size size;

  static Rectangle FromMinMaxPoints(const Point& min, const Point& max);

  bool operator==(const Rectangle& other) const;

  float MinX() const;
  float MinY() const;
  float MaxX() const;
  float MaxY() const;
  float Width() const;
  float Height() const;

  Point Min() const;
  Point Max() const;

  // Returns a rectangle that contains only the area that is shared by this
  // rectangle and another rectangle. Check that the rectangles overlap with
  // `Contains` first, otherwise a rectangle may be returned with a negative
  // size.
  Rectangle Intersection(const Rectangle& other) const;

  // Returns a rectangle that is big enough to contain both this rectangle and
  // another rectangle.
  Rectangle Union(const Rectangle& other) const;

  // Returns whether another rectangle intersect this rectangle.
  bool Intersects(const Rectangle& other) const;

  // Returns whether another rectangle is inside this rectangle. (Upper bounds
  // is exclusive.)
  bool Contains(const Rectangle& child) const;

  // Returns whether another point is inside this rectangle. (Upper bounds is
  // exclusive.)
  bool Contains(const Point& point) const;

  // Returns the rectangle, but rounded the largest bounding area using whole
  // integers. Min values round down, max values round up.
  Rectangle RoundedToLargestWholeInteger() const;
};

}  // namespace ui
}  // namespace perception
