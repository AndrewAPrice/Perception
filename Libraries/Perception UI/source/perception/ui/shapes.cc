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

#include "perception/ui/shapes.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/theme.h"

namespace perception {
namespace ui {

void DrawRightAlignedArrow(const DrawContext& draw_context) {
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(kButtonTextColor);
  paint.setStyle(SkPaint::kFill_Style);

  float x = draw_context.area.origin.x + draw_context.area.size.width - 16.0f;
  float y =
      draw_context.area.origin.y + draw_context.area.size.height / 2.0f - 2.0f;

  SkPoint pts[3] = {{x, y}, {x + 8.0f, y}, {x + 4.0f, y + 4.0f}};
  SkPath path = SkPath::Polygon({pts, 3}, true);

  draw_context.skia_canvas->drawPath(path, paint);
}

}  // namespace ui
}  // namespace perception
