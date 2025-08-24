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

#pragma once

#include <functional>

#include "perception/object_pool.h"
#include "perception/ui/point.h"
#include "perception/ui/quadtree.h"
#include "perception/ui/rectangle.h"
#include "types.h"

enum class QuadRectangleStage {
  // An opaque (no transparency) block to draw directly to the screen.
  OPAQUE_TO_SCREEN,

  // An opaque (no transparency) block to draw to the window manager.
  OPAQUE_TO_WINDOW_MANAGER,

  // An alpha blended block to draw to the window manager. This will be
  // z-sorted.
  ALPHA_TO_WINDOW_MANAGER,
};

struct QuadRectangle
    : public ::perception::ui::QuadTree<QuadRectangle>::Object {
  // The texture ID to copy to the output. May be 0 if we are a solid fill
  // color.
  size_t texture_id;

  // Coordinates in the texture to start copying from.
  ::perception::ui::Point texture_offset;

  // Fixed color to fill with, if texture_id = 0.
  uint32 color;

  // The z-ordering, for alpha blended draws.
  int z_index;

  // The draw stage.
  QuadRectangleStage stage;

  // Is this a rectangle for a solid color?
  bool IsSolidColor() { return texture_id == 0; }

  // Makes this rectangle a sub-rectangle of the other.
  void SubRectangleOf(const QuadRectangle& other) {
    stage = other.stage;
    z_index = other.z_index;
    color = other.color;
    texture_id = other.texture_id;
    if (texture_id != 0) {
      texture_offset =
          other.texture_offset + bounds.origin - other.bounds.origin;
    }
  }
};

class CompositorQuadTree : public ::perception::ui::QuadTree<QuadRectangle> {
 public:
  CompositorQuadTree() : rectangle_pool_(), QuadTree(&rectangle_pool_) {}

  virtual ~CompositorQuadTree() {}

  // Adds a rectangle, splitting any rectangle that is partially covered,
  // and removing any rectangle that is fully covered.
  void AddOccludingRectangle(QuadRectangle* rect);

  void AddRectangle(QuadRectangle* rect);

  // Tells a region that it needs to draw into the window manager's
  // texture.
  void DrawAreaToWindowManagerTexture(
      const ::perception::ui::Rectangle& screen_area);

  // Allocates a Rectangle from the object pool, for passing into
  // AddOccludingRectangle.
  QuadRectangle* AllocateRectangle();

 private:
  // Creates a sub-rectangle for each background part that is visible behind
  // the foreground. Make sure that the Rectangles at least overlap before
  // calling this.
  void CreateSubRectanglesForEachBackgroundPartThatPokesOut(
      QuadRectangle* background, QuadRectangle* foreground);

  // Creates a subrectangle that's part of the background.
  void CreateSubRectangle(QuadRectangle& background,
                          const ::perception::ui::Rectangle& bounds,
                          QuadRectangleStage stage);

  ::perception::ObjectPool<QuadRectangle> rectangle_pool_;
};
