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

#include "highlighter.h"

#include "perception/ui/rectangle.h"
#include "testing.h"

namespace {

using ::perception::ui::Rectangle;

TEST(HighlighterLifecycle) {
  InitializeHighlighter();

  Rectangle area1{.origin = {.x = 10.0f, .y = 20.0f},
                  .size = {.width = 100.0f, .height = 50.0f}};

  SetHighlighter(area1);

  // Set identical area should be no-op safely
  SetHighlighter(area1);

  // Update highlighter to new area
  Rectangle area2{.origin = {.x = 50.0f, .y = 60.0f},
                  .size = {.width = 200.0f, .height = 100.0f}};
  SetHighlighter(area2);

  // Disabling highlighter
  DisableHighlighter();

  // Disabling already disabled highlighter should be safe no-op
  DisableHighlighter();
}

TEST(HighlighterDrawIntersection) {
  InitializeHighlighter();

  Rectangle area{.origin = {.x = 10.0f, .y = 10.0f},
                 .size = {.width = 100.0f, .height = 100.0f}};
  SetHighlighter(area);

  // Drawing area completely inside
  Rectangle inside_draw_area{.origin = {.x = 20.0f, .y = 20.0f},
                             .size = {.width = 30.0f, .height = 30.0f}};
  DrawHighlighter(inside_draw_area);

  // Drawing area outside
  Rectangle outside_draw_area{.origin = {.x = 200.0f, .y = 200.0f},
                              .size = {.width = 50.0f, .height = 50.0f}};
  DrawHighlighter(outside_draw_area);

  DisableHighlighter();
  // Drawing when disabled should return early
  DrawHighlighter(inside_draw_area);
}

}  // namespace
