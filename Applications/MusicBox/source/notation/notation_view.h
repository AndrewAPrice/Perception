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

#include <algorithm>
#include <memory>

class SkCanvas;
#include "perception/ui/draw_context.h"
#include "perception/ui/node.h"
#include "perception/ui/point.h"
#include "track_manager.h"

class UndoManager;

namespace notation {

enum class NotationType { FallingNotes, Staff };

enum class DragState {
  None,
  MovingNote,
  ResizingNoteTop,
  ResizingNoteBottom,
  DrawingNote,
  Panning
};

class NotationView {
 public:
  explicit NotationView(TrackManager& track_manager, float speed = 0.12f);
  virtual ~NotationView() = default;

  // Returns the UI Node for layout placement.
  virtual std::shared_ptr<perception::ui::Node> GetNode() { return node_; }

  // Requests a redraw of the notation canvas.
  virtual void Invalidate();

  // Zoom controls.
  virtual void ZoomIn();
  virtual void ZoomOut();
  virtual void ResetZoom();
  virtual float GetZoomLevel() const { return zoom_level_; }

  // Deletes the currently hovered note, if any. Returns true if a note was
  // deleted.
  virtual bool DeleteHoveredNote();

  virtual void SetSnapTicks(int snap_ticks) { snap_ticks_ = snap_ticks; }
  virtual void SetBeatsPerBar(int beats_per_bar);
  virtual void SetNotePerBeat(int note_per_beat);
  virtual int GetBeatsPerBar() const { return beats_per_bar_; }
  virtual int GetNotePerBeat() const { return note_per_beat_; }
  virtual void SetControlDown(bool down) {
    if (control_down_ == down) return;
    control_down_ = down;
    Invalidate();
  }
  virtual bool IsControlDown() const { return control_down_; }
  virtual void SetSpaceDown(bool down) {
    space_down_ = down;
    if (!down && drag_state_ == DragState::Panning) {
      drag_state_ = DragState::None;
    }
    Invalidate();
  }
  virtual void SetInputFocused(bool focused) { input_focused_ = focused; }

  virtual void OnTrackSelected(int track_id, bool auto_scroll) {}
  void SetOnTrackSelectedCallback(
      std::function<void(int track_id, bool auto_scroll)> cb) {
    on_track_selected_cb_ = std::move(cb);
  }

  static int SnapTick(int tick, int snap_ticks) {
    return TrackManager::SnapTick(tick, snap_ticks);
  }
  int SnapTick(int tick) const;

  virtual void SetUndoManager(UndoManager* undo_manager) {
    undo_manager_ = undo_manager;
  }

  // Default zoom level scaling factor.
  static constexpr float kDefaultZoomLevel = 0.4096f;  // 1.0 / (1.25^4)

 protected:
  void TriggerPreviewSound(int key_index, const Instrument* inst, float volume,
                           float bpm);
  void StopPreviewSound();
  void NotifyTrackSelected(int track_id, bool auto_scroll);

  TrackManager& track_manager_;
  UndoManager* undo_manager_ = nullptr;
  std::shared_ptr<perception::ui::Node> node_;
  std::function<void(int track_id, bool auto_scroll)> on_track_selected_cb_;

  int snap_ticks_ = 32;                   // Default 1/8 note = 32 ticks
  int beats_per_bar_ = 4;                 // Default 4 beats per bar
  int note_per_beat_ = 4;                 // Default 4 (1/4 note)
  float speed_ = 0.12f;                   // Base pixels per millisecond
  float zoom_level_ = kDefaultZoomLevel;  // Zoom multiplier

  bool control_down_ = false;
  bool space_down_ = false;
  bool input_focused_ = false;

  perception::ui::Point last_mouse_pos_{0.0f, 0.0f};
  perception::ui::Point pan_start_mouse_pos_{0.0f, 0.0f};
  int pan_start_time_ms_ = 0;

  DragState drag_state_ = DragState::None;
  int hover_track_id_ = 0;
  int hover_note_index_ = -1;
  int drag_track_id_ = 0;
  int drag_note_index_ = -1;
  int initial_note_start_tick_ = 0;
  int initial_note_duration_ticks_ = 0;
  int initial_note_key_index_ = 0;
  NoteEvent initial_drag_note_;
  int draw_start_tick_ = 0;
  int last_preview_key_ = -1;
};

// Factory method to create a notation view implementation.
std::unique_ptr<NotationView> CreateNotationView(NotationType type,
                                                 TrackManager& track_manager);

}  // namespace notation
