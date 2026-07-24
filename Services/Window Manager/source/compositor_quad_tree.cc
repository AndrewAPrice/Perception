// Copyright 2021 Google LLC
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

#include "compositor_quad_tree.h"

#include <algorithm>

#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "perception/ui/size.h"

using ::perception::ui::Point;
using ::perception::ui::Rectangle;

// Adds a rectangle, splitting any rectangle that is partially covered,
// and removing any rectangle that is fully covered.
void CompositorQuadTree::AddOccludingRectangle(QuadRectangle* rect) {
  rect->bounds = rect->bounds.RoundedToLargestWholeInteger();
  if (rect->bounds.Width() <= 0.0f || rect->bounds.Height() <= 0.0f) {
    rectangle_pool_.Release(rect);
    return;
  }
  ForEachOverlappingItem(rect, [&](QuadRectangle* overlapping_rect) {
    // Add each part that peaks out behind our new rectangle.
    CreateSubRectanglesForEachBackgroundPartThatPokesOut(*overlapping_rect,
                                                         *rect);

    // Remove the old rectangle.
    Remove(overlapping_rect);
  });
  Add(rect);
}

void CompositorQuadTree::AddRectangle(QuadRectangle* rect) {
  rect->bounds = rect->bounds.RoundedToLargestWholeInteger();
  if (rect->bounds.Width() <= 0.0f || rect->bounds.Height() <= 0.0f) {
    rectangle_pool_.Release(rect);
    return;
  }

  Add(rect);
}

// Tells a region that it needs to draw into the window manager's
// texture.
void CompositorQuadTree::DrawAreaToWindowManagerTexture(
    const Rectangle& screen_area) {
  Rectangle rounded_area = screen_area.RoundedToLargestWholeInteger();
  if (rounded_area.size.width <= 0 || rounded_area.size.height <= 0) return;

  QuadRectangle* rect = rectangle_pool_.Allocate();
  rect->bounds = rounded_area;

  ForEachOverlappingItem(rect, [&](QuadRectangle* overlapping_rect) {
    if (overlapping_rect->stage != QuadRectangleStage::OPAQUE_TO_SCREEN) {
      // Already copying into the window manager's texture.
      return;
    }

    // Add each part that peaks out behind our new rectangle.
    CreateSubRectanglesForEachBackgroundPartThatPokesOut(*overlapping_rect,
                                                         *rect);

    // Add the part of the rectangle that is fully enclosed in our
    // region that we want to draw into the window manager's
    // texture.
    auto intersecting_rect =
        overlapping_rect->bounds.Intersection(rect->bounds);
    if (intersecting_rect) {
      CreateSubRectangle(*overlapping_rect, *intersecting_rect,
                         QuadRectangleStage::OPAQUE_TO_WINDOW_MANAGER);
    }
    // Remove the old rectangle.
    Remove(overlapping_rect);
  });

  rectangle_pool_.Release(rect);
}

// Allocates a Rectangle from the object pool, for passing into
// AddOccludingRectangle.
QuadRectangle* CompositorQuadTree::AllocateRectangle() {
  return rectangle_pool_.Allocate();
}

// Creates a sub-rectangle for each background part that is visible behind
// the foreground. Make sure that the Rectangles at least overlap before
// calling this.
void CompositorQuadTree::CreateSubRectanglesForEachBackgroundPartThatPokesOut(
    const QuadRectangle& background, const QuadRectangle& foreground) {
  QuadRectangle bg = background;
  bg.bounds = bg.bounds.RoundedToLargestWholeInteger();
  QuadRectangle fg = foreground;
  fg.bounds = fg.bounds.RoundedToLargestWholeInteger();

  // Divides the rectangle up into 4 parts that could peak out:
  // #####
  // %%i**
  // @@@@@

  // Top
  if (bg.bounds.MinY() < fg.bounds.MinY()) {
    CreateSubRectangle(
        bg,
        Rectangle::FromMinMaxPoints(Point{bg.bounds.MinX(), bg.bounds.MinY()},
                                    Point{bg.bounds.MaxX(), fg.bounds.MinY()}),
        bg.stage);
  }

  // Bottom
  if (bg.bounds.MaxY() > fg.bounds.MaxY()) {
    CreateSubRectangle(
        bg,
        Rectangle::FromMinMaxPoints(Point{bg.bounds.MinX(), fg.bounds.MaxY()},
                                    Point{bg.bounds.MaxX(), bg.bounds.MaxY()}),
        bg.stage);
  }

  // Left
  if (bg.bounds.MinX() < fg.bounds.MinX()) {
    CreateSubRectangle(bg,
                       Rectangle::FromMinMaxPoints(
                           Point{bg.bounds.MinX(),
                                 std::max(bg.bounds.MinY(), fg.bounds.MinY())},
                           Point{fg.bounds.MinX(),
                                 std::min(bg.bounds.MaxY(), fg.bounds.MaxY())}),
                       bg.stage);
  }

  // Right
  if (bg.bounds.MaxX() > fg.bounds.MaxX()) {
    CreateSubRectangle(bg,
                       Rectangle::FromMinMaxPoints(
                           Point{fg.bounds.MaxX(),
                                 std::max(bg.bounds.MinY(), fg.bounds.MinY())},
                           Point{bg.bounds.MaxX(),
                                 std::min(bg.bounds.MaxY(), fg.bounds.MaxY())}),
                       bg.stage);
  }
}

void CompositorQuadTree::CreateSubRectangle(const QuadRectangle& background,
                                            const Rectangle& bounds,
                                            QuadRectangleStage stage) {
  Rectangle rounded_bounds = bounds.RoundedToLargestWholeInteger();
  if (rounded_bounds.Width() <= 0.0f || rounded_bounds.Height() <= 0.0f) return;

  QuadRectangle* new_part = rectangle_pool_.Allocate();
  new_part->bounds = rounded_bounds;
  new_part->SubRectangleOf(background);
  new_part->stage = stage;
  Add(new_part);
}
