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
#include <string>
#include <vector>

#include "track_manager.h"

struct SongMetadata;

namespace notation {
class NotationView;
}  // namespace notation

enum class ActionType {
  Record,
  CreateTrack,
  RenameTrack,
  ReorderTracks,
  DeleteTrack,
  ChangeNote,
  DrawNote,
  DeleteNote,
  ChangeTrackColor,
  ChangeSongSettings
};

class UndoAction {
 public:
  virtual ~UndoAction() = default;

  // Returns the classification type of this undo action.
  virtual ActionType GetType() const = 0;

  // Reverts the action, restoring prior state in TrackManager and SongMetadata.
  virtual void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
                    notation::NotationView* notation_view) = 0;

  // Returns true if next_action can be compressed/coalesced into this action.
  virtual bool CanCompressWith(const UndoAction& next_action) const {
    return false;
  }

  // Merges next_action into this action to update the target state.
  virtual void CompressWith(const UndoAction& next_action) {}
};

class RecordUndoAction : public UndoAction {
 public:
  RecordUndoAction(int track_id, const std::vector<NoteEvent>& old_notes);

  ActionType GetType() const override { return ActionType::Record; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int track_id_;
  std::vector<NoteEvent> old_notes_;
};

class CreateTrackUndoAction : public UndoAction {
 public:
  explicit CreateTrackUndoAction(int created_track_id);

  ActionType GetType() const override { return ActionType::CreateTrack; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int created_track_id_;
};

class RenameTrackUndoAction : public UndoAction {
 public:
  RenameTrackUndoAction(int track_id, const std::string& old_name,
                        const std::string& new_name);

  ActionType GetType() const override { return ActionType::RenameTrack; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;
  bool CanCompressWith(const UndoAction& next_action) const override;
  void CompressWith(const UndoAction& next_action) override;

 private:
  int track_id_;
  std::string old_name_;
  std::string new_name_;
};

class ReorderTracksUndoAction : public UndoAction {
 public:
  explicit ReorderTracksUndoAction(const std::vector<int>& old_track_order);

  ActionType GetType() const override { return ActionType::ReorderTracks; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  std::vector<int> old_track_order_;
};

class DeleteTrackUndoAction : public UndoAction {
 public:
  DeleteTrackUndoAction(const Track& deleted_track, int original_index);

  ActionType GetType() const override { return ActionType::DeleteTrack; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  Track deleted_track_;
  int original_index_;
};

class ChangeNoteUndoAction : public UndoAction {
 public:
  ChangeNoteUndoAction(int track_id, const NoteEvent& old_note,
                       const NoteEvent& new_note);

  ActionType GetType() const override { return ActionType::ChangeNote; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int track_id_;
  NoteEvent old_note_;
  NoteEvent new_note_;
};

class DrawNoteUndoAction : public UndoAction {
 public:
  DrawNoteUndoAction(int track_id, const NoteEvent& drawn_note);

  ActionType GetType() const override { return ActionType::DrawNote; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int track_id_;
  NoteEvent drawn_note_;
};

class DeleteNoteUndoAction : public UndoAction {
 public:
  DeleteNoteUndoAction(int track_id, const NoteEvent& deleted_note);

  ActionType GetType() const override { return ActionType::DeleteNote; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int track_id_;
  NoteEvent deleted_note_;
};

class ChangeTrackColorUndoAction : public UndoAction {
 public:
  ChangeTrackColorUndoAction(int track_id, uint32 old_color, uint32 new_color);

  ActionType GetType() const override { return ActionType::ChangeTrackColor; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;

 private:
  int track_id_;
  uint32 old_color_;
  uint32 new_color_;
};

struct SongSettingsState {
  float bpm = 120.0f;
  int beats_per_bar = 4;
  int note_per_beat = 4;
};

class ChangeSongSettingsUndoAction : public UndoAction {
 public:
  ChangeSongSettingsUndoAction(const SongSettingsState& old_settings,
                               const SongSettingsState& new_settings);

  ActionType GetType() const override { return ActionType::ChangeSongSettings; }
  void Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view) override;
  bool CanCompressWith(const UndoAction& next_action) const override;
  void CompressWith(const UndoAction& next_action) override;

 private:
  SongSettingsState old_settings_;
  SongSettingsState new_settings_;
};

class UndoManager {
 public:
  UndoManager();

  // Pushes an action onto the undo stack, compressing with the top action if applicable.
  void PushAction(std::unique_ptr<UndoAction> action);

  // Undoes the top action on the stack. Returns true if an action was undone.
  bool Undo(TrackManager& track_manager, SongMetadata& song_metadata,
            notation::NotationView* notation_view);

  // Clears the undo stack.
  void Clear();

  // Sets a callback to be notified whenever the stack state changes.
  void SetOnStackChangedCallback(std::function<void()> on_stack_changed) {
    on_stack_changed_ = std::move(on_stack_changed);
  }

  // Returns true if there are undoable actions on the stack.
  bool CanUndo() const;

  // Returns true if an undo operation is currently executing.
  bool IsUndoing() const { return is_performing_undo_; }

  // Returns current number of actions on the stack.
  size_t Size() const;

 private:
  std::vector<std::unique_ptr<UndoAction>> stack_;
  std::function<void()> on_stack_changed_;
  bool is_performing_undo_ = false;
};
