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

#include <functional>
#include <memory>

#include "include/core/SkCanvas.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "perception/ui/size.h"
#include "track_manager.h"

namespace panels {

class TimelineRuler {
 public:
  TimelineRuler(TrackManager& track_manager, std::function<void()> on_seek);
  ~TimelineRuler() = default;

  std::shared_ptr<perception::ui::Node> GetNode() { return node_; }
  void Invalidate();

  void SetBeatsPerBar(int beats_per_bar) { beats_per_bar_ = beats_per_bar; }
  void SetNotePerBeat(int note_per_beat) { note_per_beat_ = note_per_beat; }

 private:
  void BuildUI();
  void Draw(SkCanvas& canvas, const perception::ui::Size& size);
  void HandleMouseScrub(float x, float width);

  TrackManager& track_manager_;
  std::function<void()> on_seek_;
  std::shared_ptr<perception::ui::Node> node_;

  bool is_scrubbing_ = false;
  int beats_per_bar_ = 4;
  int note_per_beat_ = 4;
};

}  // namespace panels
