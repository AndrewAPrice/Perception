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

#include "notation/notation_view.h"

#include <algorithm>

#include "constants.h"
#include "notation/falling_notes_view.h"
#include "notation/sheet_view.h"
#include "perception/window/cursor.h"
#include "synth_engine.h"
#include "undo_manager.h"

namespace notation {

namespace {

// Maximum allowed zoom scale factor.
constexpr float kMaxZoomLevel = 5.0f;

// Minimum allowed zoom scale factor.
constexpr float kMinZoomLevel = 0.2f;

// Multiplier applied when zooming in or out.
constexpr float kZoomStepFactor = 1.25f;

// Minimum allowed beats per bar.
constexpr int kMinBeatsPerBar = 1;

}  // namespace

NotationView::NotationView(TrackManager& track_manager, float speed)
    : track_manager_(track_manager), speed_(speed) {}

void NotationView::Invalidate() {
  if (node_) node_->Invalidate();
}

void NotationView::ZoomIn() {
  zoom_level_ = std::min(kMaxZoomLevel, zoom_level_ * kZoomStepFactor);
  Invalidate();
}

void NotationView::ZoomOut() {
  zoom_level_ = std::max(kMinZoomLevel, zoom_level_ / kZoomStepFactor);
  Invalidate();
}

void NotationView::ResetZoom() {
  zoom_level_ = kDefaultZoomLevel;
  Invalidate();
}

bool NotationView::DeleteHoveredNote() {
  if (track_manager_.IsPlaying()) return false;

  if (hover_track_id_ != 0 && hover_note_index_ >= 0) {
    Track* trk = track_manager_.GetTrack(hover_track_id_);
    if (trk && hover_note_index_ < static_cast<int>(trk->notes.size())) {
      int deleted_track_id = hover_track_id_;
      NoteEvent deleted_note = trk->notes[hover_note_index_];
      trk->notes.erase(trk->notes.begin() + hover_note_index_);
      if (undo_manager_) {
        undo_manager_->PushAction(
            std::make_unique<DeleteNoteUndoAction>(deleted_track_id, deleted_note));
      }
      hover_track_id_ = 0;
      hover_note_index_ = -1;
      drag_track_id_ = 0;
      drag_note_index_ = -1;
      if (node_)
        node_->SetCursor(control_down_ ? perception::window::Cursor::Eraser
                                       : perception::window::Cursor::Pen);
      NotifyTrackSelected(deleted_track_id, /*auto_scroll=*/false);
      return true;
    }
  }
  return false;
}

void NotationView::SetBeatsPerBar(int beats_per_bar) {
  beats_per_bar_ = std::max(kMinBeatsPerBar, beats_per_bar);
  Invalidate();
}

void NotationView::SetNotePerBeat(int note_per_beat) {
  note_per_beat_ = note_per_beat;
  Invalidate();
}

int NotationView::SnapTick(int tick) const {
  return TrackManager::SnapTick(tick, snap_ticks_);
}

void NotationView::TriggerPreviewSound(int key_index, const Instrument* inst,
                                       float volume, float bpm) {
  if (last_preview_key_ != -1 && last_preview_key_ != key_index) {
    NoteOff(last_preview_key_);
  }
  last_preview_key_ = key_index;
  float duration_sec = kSecondsPerMinute / (bpm > 0.0f ? bpm : kDefaultBpm);
  PlayNote(key_index, inst ? inst : GetDefaultInstrument(), volume,
           duration_sec);
}

void NotationView::StopPreviewSound() {
  if (last_preview_key_ != -1) {
    NoteOff(last_preview_key_);
    last_preview_key_ = -1;
  }
}

void NotationView::NotifyTrackSelected(int track_id, bool auto_scroll) {
  track_manager_.SetActiveTrackId(track_id);
  if (on_track_selected_cb_) {
    on_track_selected_cb_(track_id, auto_scroll);
  } else {
    OnTrackSelected(track_id, auto_scroll);
  }
}

std::unique_ptr<NotationView> CreateNotationView(NotationType type,
                                                 TrackManager& track_manager) {
  switch (type) {
    case NotationType::Staff:
      return std::make_unique<SheetView>(track_manager);
    case NotationType::FallingNotes:
    default:
      return std::make_unique<FallingNotesView>(track_manager);
  }
}

}  // namespace notation
