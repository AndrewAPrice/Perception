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

#include "testing.h"
#include "track_manager.h"

namespace {

TEST(UndoManager_CapacityLimit) {
  UndoManager undo_manager;

  for (int i = 1; i <= 105; ++i) {
    undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(i));
  }

  EXPECT((size_t)100, undo_manager.Size());
}

TEST(UndoManager_ClearStack) {
  UndoManager undo_manager;
  undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(1));
  undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(2));
  EXPECT(true, undo_manager.CanUndo());

  undo_manager.Clear();
  EXPECT(false, undo_manager.CanUndo());
  EXPECT((size_t)0, undo_manager.Size());
}

TEST(UndoManager_CompressSettingsChanges) {
  UndoManager undo_manager;
  TrackManager track_manager;
  SongMetadata metadata;

  SongSettingsState s1{.bpm = 120.0f, .beats_per_bar = 4, .note_per_beat = 4};
  SongSettingsState s2{.bpm = 130.0f, .beats_per_bar = 4, .note_per_beat = 4};
  SongSettingsState s3{.bpm = 140.0f, .beats_per_bar = 4, .note_per_beat = 4};

  undo_manager.PushAction(
      std::make_unique<ChangeSongSettingsUndoAction>(s1, s2));
  undo_manager.PushAction(
      std::make_unique<ChangeSongSettingsUndoAction>(s2, s3));

  EXPECT((size_t)1, undo_manager.Size());

  bool undone = undo_manager.Undo(track_manager, metadata, nullptr);
  EXPECT(true, undone);
  EXPECT(120.0f, track_manager.GetBpm());
}

TEST(UndoManager_UndoCreateTrack) {
  UndoManager undo_manager;
  TrackManager track_manager;
  SongMetadata metadata;

  Track* t = track_manager.AddTrack("Track 2", nullptr, 0xFF0000);
  int trk_id = t->id;
  undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(trk_id));

  EXPECT(true, undo_manager.CanUndo());
  undo_manager.Undo(track_manager, metadata, nullptr);
  EXPECT(nullptr, track_manager.GetTrack(trk_id));
}

TEST(UndoManager_OnStackChangedCallback) {
  UndoManager undo_manager;
  int change_count = 0;
  undo_manager.SetOnStackChangedCallback([&]() { ++change_count; });

  undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(1));
  EXPECT(1, change_count);

  TrackManager track_manager;
  SongMetadata metadata;
  undo_manager.Undo(track_manager, metadata, nullptr);
  EXPECT(2, change_count);

  undo_manager.PushAction(std::make_unique<CreateTrackUndoAction>(2));
  EXPECT(3, change_count);

  undo_manager.Clear();
  EXPECT(4, change_count);
}

}  // namespace
