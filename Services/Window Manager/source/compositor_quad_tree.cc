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
  if (rect->bounds.Width() <= 0.0f || rect->bounds.Height() <= 0.0f) {
    rectangle_pool_.Release(rect);
    return;
  }
  rect->node = nullptr;
  ForEachOverlappingItem(rect, [&](QuadRectangle* overlapping_rect) {
    // Add each part that peaks out behind our new rectangle.
    CreateSubRectanglesForEachBackgroundPartThatPokesOut(overlapping_rect,
                                                         rect);

    // Remove the old rectangle.
    Remove(overlapping_rect);
  });

  Add(rect);
}

void CompositorQuadTree::AddRectangle(QuadRectangle* rect) {
  if (rect->bounds.Width() <= 0.0f || rect->bounds.Height() <= 0.0f) {
    rectangle_pool_.Release(rect);
    return;
  }
  rect->node = nullptr;
  Add(rect);
}

// Tells a region that it needs to draw into the window manager's
// texture.
void CompositorQuadTree::DrawAreaToWindowManagerTexture(
    const Rectangle& screen_area) {
  if (screen_area.size.width <= 0 || screen_area.size.height <= 0) return;

  QuadRectangle* rect = rectangle_pool_.Allocate();
  rect->bounds = screen_area;
  rect->node = nullptr;

  ForEachOverlappingItem(rect, [&](QuadRectangle* overlapping_rect) {
    if (overlapping_rect->stage != QuadRectangleStage::OPAQUE_TO_SCREEN) {
      // Already copying into the window manager's texture.
      return;
    }

    // Add each part that peaks out behind our new rectangle.
    CreateSubRectanglesForEachBackgroundPartThatPokesOut(overlapping_rect,
                                                         rect);

    // Add the part of the rectangle that is fully enclosed in our
    // region that we want to draw into the window manager's
    // texture.
    CreateSubRectangle(*overlapping_rect,
                       overlapping_rect->bounds.Intersection(rect->bounds),
                       QuadRectangleStage::OPAQUE_TO_WINDOW_MANAGER);
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
    QuadRectangle* background, QuadRectangle* foreground) {
  // Divides the rectangle up into 4 parts that could peak out:
  // #####
  // %%i**
  // @@@@@

  // Top
  if (background->bounds.MinY() < foreground->bounds.MinY()) {
    CreateSubRectangle(
        *background,
        Rectangle::FromMinMaxPoints(
            Point{background->bounds.MinX(), background->bounds.MinY()},
            Point{background->bounds.MaxX(), foreground->bounds.MinY()}),
        background->stage);
  }

  // Bottom
  if (background->bounds.MaxY() > foreground->bounds.MaxY()) {
    CreateSubRectangle(
        *background,
        Rectangle::FromMinMaxPoints(
            Point{background->bounds.MinX(), foreground->bounds.MaxY()},
            Point{background->bounds.MaxX(), background->bounds.MaxY()}),
        background->stage);
  }

  // Left
  if (background->bounds.MinX() < foreground->bounds.MinX()) {
    CreateSubRectangle(
        *background,
        Rectangle::FromMinMaxPoints(Point{background->bounds.MinX(),
                                          std::max(background->bounds.MinY(),
                                                   foreground->bounds.MinY())},
                                    Point{foreground->bounds.MinX(),
                                          std::min(background->bounds.MaxY(),
                                                   foreground->bounds.MaxY())}),
        background->stage);
  }

  // Right
  if (background->bounds.MaxX() > foreground->bounds.MaxX()) {
    CreateSubRectangle(
        *background,
        Rectangle::FromMinMaxPoints(Point{foreground->bounds.MaxX(),
                                          std::max(background->bounds.MinY(),
                                                   foreground->bounds.MinY())},
                                    Point{background->bounds.MaxX(),
                                          std::min(background->bounds.MaxY(),
                                                   foreground->bounds.MaxY())}),
        background->stage);
  }
}

void CompositorQuadTree::CreateSubRectangle(QuadRectangle& background,
                                            const Rectangle& bounds,
                                            QuadRectangleStage stage) {
  QuadRectangle* new_part = rectangle_pool_.Allocate();
  new_part->node = nullptr;
  new_part->bounds = bounds;
  new_part->SubRectangleOf(background);
  new_part->stage = stage;
  Add(new_part);
}