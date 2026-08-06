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

#include "undo_manager.h"

#include <algorithm>
#include <utility>

namespace notation {
class NotationView {
 public:
  virtual ~NotationView() = default;
  virtual void SetBeatsPerBar(int beats_per_bar) = 0;
  virtual void SetNotePerBeat(int note_per_beat) = 0;
};
}  // namespace notation

namespace {

// Maximum number of undo actions preserved on the stack.
constexpr size_t kMaxUndoStackCapacity = 100;

bool AreNotesEqual(const NoteEvent& a, const NoteEvent& b) {
  return a.key_index == b.key_index && a.start_tick == b.start_tick &&
         a.duration_ticks == b.duration_ticks;
}

}  // namespace

RecordUndoAction::RecordUndoAction(int track_id,
                                   const std::vector<NoteEvent>& old_notes)
    : track_id_(track_id), old_notes_(old_notes) {}

void RecordUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                            notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  t->notes = old_notes_;
  track_manager.SyncTrackNotesMs(*t);
}

CreateTrackUndoAction::CreateTrackUndoAction(int created_track_id)
    : created_track_id_(created_track_id) {}

void CreateTrackUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                notation::NotationView*) {
  track_manager.DeleteTrack(created_track_id_);
}

RenameTrackUndoAction::RenameTrackUndoAction(int track_id,
                                             const std::string& old_name,
                                             const std::string& new_name)
    : track_id_(track_id), old_name_(old_name), new_name_(new_name) {}

void RenameTrackUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                 notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  t->name = old_name_;
}

bool RenameTrackUndoAction::CanCompressWith(
    const UndoAction& next_action) const {
  if (next_action.GetType() != ActionType::RenameTrack) return false;
  const auto& rename = static_cast<const RenameTrackUndoAction&>(next_action);
  return rename.track_id_ == track_id_;
}

void RenameTrackUndoAction::CompressWith(const UndoAction& next_action) {
  const auto& rename = static_cast<const RenameTrackUndoAction&>(next_action);
  new_name_ = rename.new_name_;
}

ReorderTracksUndoAction::ReorderTracksUndoAction(
    const std::vector<int>& old_track_order)
    : old_track_order_(old_track_order) {}

void ReorderTracksUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                   notation::NotationView*) {
  auto& tracks = track_manager.GetTracks();
  std::vector<Track> new_list;
  new_list.reserve(tracks.size());

  for (int id : old_track_order_) {
    for (auto it = tracks.begin(); it != tracks.end(); ++it) {
      if (it->id == id) {
        new_list.push_back(std::move(*it));
        tracks.erase(it);
        break;
      }
    }
  }
  for (auto& remaining : tracks) new_list.push_back(std::move(remaining));
  tracks = std::move(new_list);
}

DeleteTrackUndoAction::DeleteTrackUndoAction(const Track& deleted_track,
                                             int original_index)
    : deleted_track_(deleted_track), original_index_(original_index) {}

void DeleteTrackUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                 notation::NotationView*) {
  auto& tracks = track_manager.GetTracks();
  int idx = std::clamp(original_index_, 0, static_cast<int>(tracks.size()));
  tracks.insert(tracks.begin() + idx, deleted_track_);
  track_manager.SetActiveTrackId(deleted_track_.id);
}

ChangeNoteUndoAction::ChangeNoteUndoAction(int track_id,
                                           const NoteEvent& old_note,
                                           const NoteEvent& new_note)
    : track_id_(track_id), old_note_(old_note), new_note_(new_note) {}

void ChangeNoteUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  for (auto& note : t->notes) {
    if (AreNotesEqual(note, new_note_)) {
      note = old_note_;
      track_manager.SyncTrackNotesMs(*t);
      return;
    }
  }
}

DrawNoteUndoAction::DrawNoteUndoAction(int track_id, const NoteEvent& drawn_note)
    : track_id_(track_id), drawn_note_(drawn_note) {}

void DrawNoteUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                              notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  for (auto it = t->notes.begin(); it != t->notes.end(); ++it) {
    if (AreNotesEqual(*it, drawn_note_)) {
      t->notes.erase(it);
      track_manager.SyncTrackNotesMs(*t);
      return;
    }
  }
}

DeleteNoteUndoAction::DeleteNoteUndoAction(int track_id,
                                           const NoteEvent& deleted_note)
    : track_id_(track_id), deleted_note_(deleted_note) {}

void DeleteNoteUndoAction::Undo(TrackManager& track_manager, SongMetadata&,
                                notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  t->notes.push_back(deleted_note_);
  track_manager.SyncTrackNotesMs(*t);
}

ChangeTrackColorUndoAction::ChangeTrackColorUndoAction(int track_id,
                                                       uint32 old_color,
                                                       uint32 new_color)
    : track_id_(track_id), old_color_(old_color), new_color_(new_color) {}

void ChangeTrackColorUndoAction::Undo(TrackManager& track_manager,
                                      SongMetadata&,
                                      notation::NotationView*) {
  Track* t = track_manager.GetTrack(track_id_);
  if (!t) return;
  t->color = old_color_;
}

ChangeSongSettingsUndoAction::ChangeSongSettingsUndoAction(
    const SongSettingsState& old_settings,
    const SongSettingsState& new_settings)
    : old_settings_(old_settings), new_settings_(new_settings) {}

void ChangeSongSettingsUndoAction::Undo(TrackManager& track_manager,
                                        SongMetadata& song_metadata,
                                        notation::NotationView* notation_view) {
  track_manager.SetBpm(old_settings_.bpm);
  song_metadata.bpm = old_settings_.bpm;
  song_metadata.beats_per_bar = old_settings_.beats_per_bar;
  song_metadata.note_per_beat = old_settings_.note_per_beat;

  if (notation_view) {
    notation_view->SetBeatsPerBar(old_settings_.beats_per_bar);
    notation_view->SetNotePerBeat(old_settings_.note_per_beat);
  }
}

bool ChangeSongSettingsUndoAction::CanCompressWith(
    const UndoAction& next_action) const {
  return next_action.GetType() == ActionType::ChangeSongSettings;
}

void ChangeSongSettingsUndoAction::CompressWith(const UndoAction& next_action) {
  const auto& settings =
      static_cast<const ChangeSongSettingsUndoAction&>(next_action);
  new_settings_ = settings.new_settings_;
}

UndoManager::UndoManager() = default;

void UndoManager::PushAction(std::unique_ptr<UndoAction> action) {
  if (!action || is_performing_undo_) return;
  if (!stack_.empty() && stack_.back()->CanCompressWith(*action)) {
    stack_.back()->CompressWith(*action);
    if (on_stack_changed_) on_stack_changed_();
    return;
  }
  stack_.push_back(std::move(action));
  if (stack_.size() > kMaxUndoStackCapacity) {
    stack_.erase(stack_.begin());
  }
  if (on_stack_changed_) on_stack_changed_();
}

bool UndoManager::Undo(TrackManager& track_manager,
                       SongMetadata& song_metadata,
                       notation::NotationView* notation_view) {
  if (stack_.empty() || is_performing_undo_) return false;
  is_performing_undo_ = true;
  auto action = std::move(stack_.back());
  stack_.pop_back();
  action->Undo(track_manager, song_metadata, notation_view);
  is_performing_undo_ = false;
  if (on_stack_changed_) on_stack_changed_();
  return true;
}

void UndoManager::Clear() {
  bool was_empty = stack_.empty();
  stack_.clear();
  if (!was_empty && on_stack_changed_) on_stack_changed_();
}

bool UndoManager::CanUndo() const { return !stack_.empty(); }

size_t UndoManager::Size() const { return stack_.size(); }
