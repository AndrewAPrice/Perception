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

#include "highlighter.h"

#include "compositor.h"
#include "perception/draw.h"
#include "perception/ui/rectangle.h"
#include "screen.h"

using ::perception::ui::Rectangle;
namespace graphics = ::perception::devices::graphics;

namespace {

bool highlighter_enabled;
Rectangle highlighter_area;

}  // namespace

void InitializeHighlighter() { highlighter_enabled = false; }

void SetHighlighter(const Rectangle& screen_area) {
  if (highlighter_enabled) {
    if (highlighter_area == screen_area) return;

    InvalidateScreen(highlighter_area);
  }

  highlighter_area = screen_area;
  highlighter_enabled = true;

  InvalidateScreen(highlighter_area);
}

void DisableHighlighter() {
  if (!highlighter_enabled) return;

  highlighter_enabled = false;
  InvalidateScreen(highlighter_area);
}

void DrawHighlighter(const Rectangle& draw_area) {
  if (!highlighter_enabled) return;
  if (!draw_area.Intersects(highlighter_area)) {
    // The highlighting is outside of the draw area.
    return;
  }

  auto drawing_intersection = draw_area.Intersection(highlighter_area);
  DrawAlphaBlendedColor(drawing_intersection, HIGHLIGHTER_TINT);
}
