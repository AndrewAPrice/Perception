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

#include <memory>

#include "notation/notation_view.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"
#include "track_manager.h"

namespace notation {

class FallingNotesView : public NotationView {
 public:
  explicit FallingNotesView(TrackManager& track_manager);
  ~FallingNotesView() override = default;

  static int GetKeyIndexAtX(float x, float width);
  static int GetTimeMsAtY(float y, float height, int view_start_ms,
                          float effective_speed);

  void OnTrackSelected(int track_id, bool auto_scroll) override {
    Invalidate();
  }

 private:
  struct NoteHitResult {
    int track_id = 0;
    int note_index = -1;
    bool is_top_edge = false;
    bool is_bottom_edge = false;
  };

  void BuildNode();
  void DrawFallingNotes(SkCanvas& canvas, float width, float height);

  NoteHitResult HitTestNotes(float x, float y, float width, float height);
  int GetTimeMsAtY(float y, float height);
};

}  // namespace notation
