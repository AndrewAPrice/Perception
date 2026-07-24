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

#include "compositor_quad_tree.h"

#include "perception/ui/point.h"
#include "perception/ui/rectangle.h"
#include "testing.h"

namespace {

using ::perception::ui::Point;
using ::perception::ui::Rectangle;

TEST(CompositorQuadTreeAllocateAndAddRectangle) {
  CompositorQuadTree tree;

  QuadRectangle* rect1 = tree.AllocateRectangle();
  ASSERT(true, rect1 != nullptr);
  rect1->bounds = Rectangle{Point{10.0f, 10.0f}, {100.0f, 50.0f}};
  rect1->color = 0xFF112233;
  rect1->texture_id = 0;
  rect1->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;

  tree.AddRectangle(rect1);

  int count = 0;
  tree.ForEachItem([&](QuadRectangle* item) {
    count++;
    EXPECT(10.0f, item->bounds.origin.x);
    EXPECT(10.0f, item->bounds.origin.y);
    EXPECT(100.0f, item->bounds.size.width);
    EXPECT(50.0f, item->bounds.size.height);
    EXPECT(0xFF112233, item->color);
  });
  EXPECT(1, count);
}

TEST(CompositorQuadTreeZeroSizeRectanglesReleased) {
  CompositorQuadTree tree;

  QuadRectangle* zero_width = tree.AllocateRectangle();
  zero_width->bounds = Rectangle{Point{0.0f, 0.0f}, {0.0f, 50.0f}};
  tree.AddRectangle(zero_width);

  QuadRectangle* zero_height = tree.AllocateRectangle();
  zero_height->bounds = Rectangle{Point{0.0f, 0.0f}, {50.0f, 0.0f}};
  tree.AddOccludingRectangle(zero_height);

  int count = 0;
  tree.ForEachItem([&](QuadRectangle*) { count++; });
  EXPECT(0, count);
}

TEST(CompositorQuadTreeAddOccludingRectangleNonOverlapping) {
  CompositorQuadTree tree;

  QuadRectangle* rect1 = tree.AllocateRectangle();
  rect1->bounds = Rectangle{Point{0.0f, 0.0f}, {50.0f, 50.0f}};
  rect1->color = 0xFF111111;
  rect1->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  tree.AddOccludingRectangle(rect1);

  QuadRectangle* rect2 = tree.AllocateRectangle();
  rect2->bounds = Rectangle{Point{100.0f, 100.0f}, {50.0f, 50.0f}};
  rect2->color = 0xFF222222;
  rect2->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  tree.AddOccludingRectangle(rect2);

  int count = 0;
  tree.ForEachItem([&](QuadRectangle*) { count++; });
  EXPECT(2, count);
}

TEST(CompositorQuadTreeAddOccludingRectangleFullCover) {
  CompositorQuadTree tree;

  // Background rectangle
  QuadRectangle* bg = tree.AllocateRectangle();
  bg->bounds = Rectangle{Point{10.0f, 10.0f}, {50.0f, 50.0f}};
  bg->color = 0xFF0000FF;
  bg->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  tree.AddOccludingRectangle(bg);

  // Larger foreground rectangle completely covering bg
  QuadRectangle* fg = tree.AllocateRectangle();
  fg->bounds = Rectangle{Point{0.0f, 0.0f}, {100.0f, 100.0f}};
  fg->color = 0xFFFF0000;
  fg->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  tree.AddOccludingRectangle(fg);

  int count = 0;
  tree.ForEachItem([&](QuadRectangle* item) {
    count++;
    EXPECT(0xFFFF0000, item->color);
  });
  EXPECT(1, count);
}

TEST(CompositorQuadTreeAddOccludingRectanglePartialCoverSplitting) {
  CompositorQuadTree tree;

  // Background rectangle from (0,0) to (100,100)
  QuadRectangle* bg = tree.AllocateRectangle();
  bg->bounds = Rectangle{Point{0.0f, 0.0f}, {100.0f, 100.0f}};
  bg->color = 0xFF00FF00;
  bg->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  bg->texture_id = 5;
  bg->texture_offset = Point{10.0f, 10.0f};
  tree.AddOccludingRectangle(bg);

  // Center foreground rectangle covering (25,25) to (75,75)
  QuadRectangle* fg = tree.AllocateRectangle();
  fg->bounds = Rectangle{Point{25.0f, 25.0f}, {50.0f, 50.0f}};
  fg->color = 0xFFFF0000;
  fg->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  fg->texture_id = 0;
  tree.AddOccludingRectangle(fg);

  int count = 0;
  tree.ForEachItem([&](QuadRectangle*) { count++; });
  // Should split background into 4 surrounding sub-rectangles (top, bottom, left, right) + 1 foreground
  EXPECT(5, count);
}

TEST(CompositorQuadTreeDrawAreaToWindowManagerTexture) {
  CompositorQuadTree tree;

  QuadRectangle* rect = tree.AllocateRectangle();
  rect->bounds = Rectangle{Point{0.0f, 0.0f}, {100.0f, 100.0f}};
  rect->color = 0xFFFFFFFF;
  rect->stage = QuadRectangleStage::OPAQUE_TO_SCREEN;
  tree.AddOccludingRectangle(rect);

  // Intersecting region (10, 10, 20, 20) requested for WM texture
  tree.DrawAreaToWindowManagerTexture(Rectangle{Point{10.0f, 10.0f}, {20.0f, 20.0f}});

  int wm_stage_count = 0;
  int screen_stage_count = 0;

  tree.ForEachItem([&](QuadRectangle* item) {
    if (item->stage == QuadRectangleStage::OPAQUE_TO_WINDOW_MANAGER) {
      wm_stage_count++;
      EXPECT(10.0f, item->bounds.origin.x);
      EXPECT(10.0f, item->bounds.origin.y);
      EXPECT(20.0f, item->bounds.size.width);
      EXPECT(20.0f, item->bounds.size.height);
    } else if (item->stage == QuadRectangleStage::OPAQUE_TO_SCREEN) {
      screen_stage_count++;
    }
  });

  EXPECT(1, wm_stage_count);
  EXPECT(4, screen_stage_count);
}

}  // namespace
