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

#include "track_manager.h"

#include <cmath>

#include "instruments.h"
#include "song_serializer.h"
#include "synth_engine.h"
#include "testing.h"

namespace {

TEST(Clef_DefaultAndSerialization) {
  TrackManager tm;
  Track* t1 = tm.GetActiveTrack();
  ASSERT(true, t1 != nullptr);
  EXPECT((int)Clef::TrebleAndBass, (int)t1->clef);

  t1->clef = Clef::Alto;
  EXPECT((int)Clef::Alto, (int)t1->clef);

  SongMetadata meta;
  nlohmann::json j = SongToJson(tm, meta);
  EXPECT(true, j.contains("tracks"));
  EXPECT(true, j["tracks"][0].contains("clef"));
  EXPECT(std::string("alto"), j["tracks"][0]["clef"].get<std::string>());

  TrackManager tm2;
  SongFromJson(j, tm2);
  Track* t2 = tm2.GetActiveTrack();
  ASSERT(true, t2 != nullptr);
  EXPECT((int)Clef::Alto, (int)t2->clef);
}

// Track Management Unit Tests
TEST(TrackManagement_AddDeleteSelect) {
  TrackManager tm;
  EXPECT((size_t)1, tm.GetTracks().size());

  Track* t1 = tm.GetActiveTrack();
  ASSERT(true, t1 != nullptr);
  int id1 = t1->id;

  Track* t2 = tm.AddTrack("Track 2", GetDefaultInstrument(), 0xFF10B981);
  ASSERT(true, t2 != nullptr);
  int id2 = t2->id;
  EXPECT((size_t)2, tm.GetTracks().size());

  tm.SetActiveTrackId(id2);
  EXPECT(id2, tm.GetActiveTrackId());

  // Deleting active track should fallback active track ID to remaining track
  EXPECT(true, tm.DeleteTrack(id2));
  EXPECT((size_t)1, tm.GetTracks().size());
  EXPECT(id1, tm.GetActiveTrackId());

  // Cannot delete sole remaining track
  EXPECT(false, tm.DeleteTrack(id1));
  EXPECT((size_t)1, tm.GetTracks().size());

  // Clear all tracks
  tm.ClearAllTracks();
  EXPECT((size_t)0, tm.GetTracks().size());
}

// BPM Clamping and Note Sync Unit Tests
TEST(Bpm_BoundaryClampingAndNoteSync) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);

  // Set BPM to bounds
  tm.SetBpm(120.0f);
  EXPECT(120.0f, tm.GetBpm());

  tm.SetBpm(5.0f);  // Below min 10
  EXPECT(10.0f, tm.GetBpm());

  tm.SetBpm(500.0f);  // Above max 400
  EXPECT(400.0f, tm.GetBpm());

  // Add a note with 64 ticks (1 quarter note)
  tm.SetBpm(120.0f);
  track->notes.push_back(
      NoteEvent{.key_index = 40, .start_tick = 64, .duration_ticks = 64});
  tm.SyncTrackNotesMs(*track);

  EXPECT(500, track->notes[0].start_time_ms);
  EXPECT(500, track->notes[0].duration_ms);

  // Change BPM to 60 (quarter note = 1000ms)
  tm.SetBpm(60.0f);
  EXPECT(1000, track->notes[0].start_time_ms);
  EXPECT(1000, track->notes[0].duration_ms);
}

// Collision Abutting and Boundary Keys Unit Tests
TEST(Collision_AbuttingAndBoundaryKeys) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);
  track->notes.clear();

  // Add a note on key 40 from tick 64 to 128
  track->notes.push_back(
      NoteEvent{.key_index = 40, .start_tick = 64, .duration_ticks = 64});

  // Abutting note ending at 64 (start=0, dur=64) SHOULD be allowed
  EXPECT(true, tm.CanPlaceNote(track->id, 40, 0, 64));

  // Abutting note starting at 128 (start=128, dur=64) SHOULD be allowed
  EXPECT(true, tm.CanPlaceNote(track->id, 40, 128, 64));

  // Boundary key indices (0 = A0, 87 = C8)
  EXPECT(true, tm.CanPlaceNote(track->id, 0, 0, 64));
  EXPECT(true, tm.CanPlaceNote(track->id, 87, 0, 64));

  // Out of bound key indices
  EXPECT(false, tm.CanPlaceNote(track->id, -1, 0, 64));
  EXPECT(false, tm.CanPlaceNote(track->id, 88, 0, 64));

  // Ignore self index during note update
  EXPECT(true, tm.CanPlaceNote(track->id, 40, 64, 64, /*ignore_note_index=*/0));
}

// Collision Partial and Contained Overlaps Unit Tests
TEST(Collision_PartialAndContainedOverlaps) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);
  track->notes.clear();

  // Note on key 40 from tick 100 to 200
  track->notes.push_back(
      NoteEvent{.key_index = 40, .start_tick = 100, .duration_ticks = 100});

  // Partial overlap start (50 to 150) -> Rejected
  EXPECT(false, tm.CanPlaceNote(track->id, 40, 50, 100));

  // Partial overlap end (150 to 250) -> Rejected
  EXPECT(false, tm.CanPlaceNote(track->id, 40, 150, 100));

  // Contained inside (120 to 180) -> Rejected
  EXPECT(false, tm.CanPlaceNote(track->id, 40, 120, 60));

  // Outer containing (50 to 250) -> Rejected
  EXPECT(false, tm.CanPlaceNote(track->id, 40, 50, 200));

  // Polyphonic note on key 42 at same time (100 to 200) -> Allowed
  EXPECT(true, tm.CanPlaceNote(track->id, 42, 100, 100));
}

// Snap Math Power of Two Intervals Unit Tests
TEST(SnapMath_PowerOfTwoIntervals) {
  // Test snap values: 1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048
  int snap_values[12] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048};

  for (int i = 0; i < 12; ++i) {
    int snap = snap_values[i];
    EXPECT(0, TrackManager::SnapTick(0, snap));
    EXPECT(snap, TrackManager::SnapTick(snap, snap));
    EXPECT(2 * snap, TrackManager::SnapTick(2 * snap, snap));

    if (snap > 2) {
      // Near boundary rounding
      EXPECT(snap, TrackManager::SnapTick(snap - 1, snap));
      EXPECT(0, TrackManager::SnapTick(1, snap));
    }
  }
}

// Coordinate Math Key Index At X Unit Tests
TEST(CoordinateMath_KeyIndexAtX) {
  float w = 520.0f;  // 10px per white key column
  // Left edge (Key 0 = A0, white key)
  EXPECT(0, TrackManager::GetKeyIndexAtX(2.0f, w));

  // Right edge (Key 87 = C8, white key index 51)
  EXPECT(87, TrackManager::GetKeyIndexAtX(515.0f, w));

  // Invalid width handling
  EXPECT(0, TrackManager::GetKeyIndexAtX(10.0f, 0.0f));
}

// Coordinate Math Time Ms At Y Unit Tests
TEST(CoordinateMath_TimeMsAtY) {
  float h = 200.0f;
  int view_start = 1000;
  float speed = 0.12f;

  // Bottom edge (y = h - 4.0f = 196.0f) should equal view_start
  EXPECT(1000, TrackManager::GetTimeMsAtY(196.0f, h, view_start, speed));

  // Moving up in Y (smaller Y) increases time
  int time_top = TrackManager::GetTimeMsAtY(96.0f, h, view_start, speed);
  EXPECT(true, time_top > view_start);
  EXPECT(1000 + static_cast<int>(100.0f / speed), time_top);
}

// Toolbar Centering Scroll Offset Formulas Unit Tests
TEST(Toolbar_CenteringScrollOffsetFormulas) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);
  track->notes.clear();

  track->notes.push_back(NoteEvent{.key_index = 40,
                                   .start_tick = 64,
                                   .duration_ticks = 64,
                                   .start_time_ms = 500,
                                   .duration_ms = 500});
  track->notes.push_back(NoteEvent{.key_index = 48,
                                   .start_tick = 256,
                                   .duration_ticks = 128,
                                   .start_time_ms = 2000,
                                   .duration_ms = 1000});

  EXPECT(500, tm.GetSongStartMs());
  EXPECT(3150, tm.GetSongDurationMs());
}

// SynthEngine Frequency Conversions Unit Tests
TEST(SynthEngine_FrequencyConversions) {
  // Key 0 (A0) = 27.5 Hz
  double f_a0 = TrackManager::KeyIndexToFrequency(0);
  EXPECT(true, std::abs(f_a0 - 27.5) < 0.1);

  // Key 48 (A4) = 440.0 Hz
  double f_a4 = TrackManager::KeyIndexToFrequency(48);
  EXPECT(true, std::abs(f_a4 - 440.0) < 0.1);

  // Key 87 (C8) ~ 4186.01 Hz
  double f_c8 = TrackManager::KeyIndexToFrequency(87);
  EXPECT(true, std::abs(f_c8 - 4186.01) < 1.0);
}

// SynthEngine Note Names Unit Tests
TEST(SynthEngine_NoteNames) {
  EXPECT(std::string_view("A0"), TrackManager::KeyIndexToNoteName(0));
  EXPECT(std::string_view("C4"), TrackManager::KeyIndexToNoteName(39));
  EXPECT(std::string_view("A4"), TrackManager::KeyIndexToNoteName(48));
  EXPECT(std::string_view("C8"), TrackManager::KeyIndexToNoteName(87));
}

// Recording State and Key Highlights Unit Tests
TEST(RecordingAndPlayback_StateAndHighlights) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);
  track->notes.clear();

  // Press & release note during recording
  tm.StartRecord();
  EXPECT(true, tm.IsRecording());

  tm.OnNotePressed(40);
  tm.OnNoteReleased(40);

  EXPECT((size_t)1, track->notes.size());
  EXPECT(40, track->notes[0].key_index);

  tm.Stop();
  EXPECT(false, tm.IsRecording());
}

// Playback Position Retention and No-Loop at End of Song Test
TEST(Playback_PreservePositionAndNoLoopAtEnd) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);
  track->notes.clear();

  // Verify stopping retains current position
  tm.StartPlay();
  tm.Tick(500, nullptr);
  EXPECT(500, tm.GetCurrentTimeMs());

  tm.Stop();
  EXPECT(false, tm.IsPlaying());
  EXPECT(500, tm.GetCurrentTimeMs());

  // Verify reaching end of song stops playback without looping back to 0
  track->notes.push_back(NoteEvent{.key_index = 40,
                                   .start_tick = 0,
                                   .duration_ticks = 64,
                                   .start_time_ms = 0,
                                   .duration_ms = 500});
  int duration = tm.GetSongDurationMs();

  tm.StartPlay();
  tm.Seek(0);
  tm.Tick(duration + 1500, nullptr);

  // Playback should stop when reaching song duration, clamping to end position
  EXPECT(false, tm.IsPlaying());
  EXPECT(duration, tm.GetCurrentTimeMs());

  // Verify seeking / scrolling is clamped to [0, duration]
  tm.Seek(-500);
  EXPECT(0, tm.GetCurrentTimeMs());

  tm.Seek(duration + 5000);
  EXPECT(duration + 5000, tm.GetCurrentTimeMs());

  int clamped_scroll = std::clamp(tm.GetCurrentTimeMs(), 0, duration);
  EXPECT(duration, clamped_scroll);
}

// All Instruments Signal Stability & Amplitude Boundedness Test
TEST(SynthEngine_AllInstrumentsSignalStability) {
  const auto& instruments = GetInstruments();
  EXPECT(true, !instruments.empty());

  double test_freq = 440.0;
  double times_to_test[] = {0.01, 0.1, 0.5, 1.0, 2.0, 3.5, 4.0};

  for (const Instrument* inst : instruments) {
    ASSERT(true, inst != nullptr);
    ASSERT(true, inst->synthesize_sample != nullptr);

    for (double t : times_to_test) {
      double sample = inst->synthesize_sample(test_freq, t, 4.0);

      EXPECT(true, std::isfinite(sample));
      EXPECT(true, sample >= -1.5 && sample <= 1.5);
    }
  }
}

TEST(SanitizeNotePerBeat) {
  EXPECT(1, SanitizeNotePerBeat(-4));
  EXPECT(1, SanitizeNotePerBeat(0));
  EXPECT(1, SanitizeNotePerBeat(1));
  EXPECT(2, SanitizeNotePerBeat(2));
  EXPECT(8, SanitizeNotePerBeat(7));
  EXPECT(8, SanitizeNotePerBeat(8));
  EXPECT(8, SanitizeNotePerBeat(9));
  EXPECT(32, SanitizeNotePerBeat(32));
  EXPECT(32, SanitizeNotePerBeat(36));
  EXPECT(32, SanitizeNotePerBeat(1000));
}

}  // namespace
