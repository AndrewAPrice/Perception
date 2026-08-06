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

#pragma once

#include "include/core/SkCanvas.h"
#include "instruments.h"
#include "notation/notation_composition.h"
#include "types.h"

namespace notation {

struct NoteRenderStyle {
  uint32 note_color = 0xFF38BDF8;
  bool is_hovered = false;
  bool is_selected = false;
  float scale = 1.0f;
  Clef clef = Clef::TrebleAndBass;
};

// Fast-path rendering for a hovered note as a 2D solid block (bypassing
// decomposition).
void DrawHoveredNoteBlock(SkCanvas& canvas, int key_index, float start_x,
                          float dur_px, float staff_center_y,
                          float line_spacing, float header_w, float view_w,
                          const NoteRenderStyle& style);

// Renders a single decomposed symbol component (notehead, stem, flag,
// accidental, dot, ledger lines, and connecting tie arc if
// component.ties_to_next is true).
void DrawSymbolComponentOnStaff(
    SkCanvas& canvas, const NoteSymbolComponent& component, int key_index,
    float cursor_x, float pixels_per_tick, int cur_time_ms, float bpm,
    float staff_center_y, float line_spacing, float header_w, float view_w,
    int beats_per_bar, const NoteRenderStyle& style);

}  // namespace notation
