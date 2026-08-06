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

#include <map>
#include <memory>

#include "notation/notation_view.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"
#include "track_manager.h"

namespace notation {

// Left margin offset in pixels before bar contents.
constexpr float kBarLeftMarginPx = 18.0f;
// Right margin offset in pixels after bar contents.
constexpr float kBarRightMarginPx = 10.0f;

// Layout position calculation helpers
float CalculateAbsoluteTickPosition(int tick, float pixels_per_tick,
                                    int beats_per_bar);
float CalculateAbsoluteBarLinePosition(int bar_index, float pixels_per_tick,
                                       int beats_per_bar);
float GetTickX(int tick, int cur_tick, float cursor_x, float pixels_per_tick,
               int beats_per_bar);
float GetBarLineX(int bar_index, int cur_tick, float cursor_x,
                  float pixels_per_tick, int beats_per_bar);
int GetTickAtX(float x, int cur_tick, float cursor_x, float pixels_per_tick,
               int beats_per_bar);

class SheetView : public NotationView {
 public:
  explicit SheetView(TrackManager& track_manager);
  ~SheetView() override = default;

  void CenterOnTrack(int track_id);
  void OnTrackSelected(int track_id, bool auto_scroll) override;

 private:
  struct NoteHitResult {
    int track_id = 0;
    int note_index = -1;
    bool is_left_edge = false;
    bool is_right_edge = false;
  };

  void BuildNode();
  void DrawSheetView(SkCanvas& canvas, float width, float height);

  NoteHitResult HitTestNotes(float x, float y, float width, float height);
  int GetKeyIndexAtY(float y, float staff_center_y, float line_spacing);
  int GetTimeMsAtX(float x, float width);

  float scroll_y_offset_px_ = 0.0f;
  float pan_start_scroll_y_ = 0.0f;
};

using StaffView = SheetView;

}  // namespace notation
