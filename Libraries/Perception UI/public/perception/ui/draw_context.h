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

#include "perception/ui/rectangle.h"
#include "types.h"

class SkCanvas;

namespace perception {
namespace ui {

struct DrawContext {
  // The raw, low level buffer.
  uint32* buffer;

  // The width of the buffer, in pixels.
  int buffer_width;

  // The height of the buffer, in pixels.
  int buffer_height;

  // The clipping boundaries. Anything outside of these boundaries isn't
  // guaranteed to be shown on screen.
  Rectangle clipping_bounds;

  // The area to draw into.
  Rectangle area;

  // The Skia canvas.
  SkCanvas* skia_canvas;

  // The window being drawn into.
};

}  // namespace ui
}  // namespace perception
