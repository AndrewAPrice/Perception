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
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "synth_engine.h"
#include "types.h"

struct NoteEvent {
  int key_index = 0;        // 0 to 87
  int start_tick = 0;       // Start time in 1/256th note units
  int duration_ticks = 32;  // Duration in 1/256th note units
  int start_time_ms = 0;    // Start time relative to track start in ms
  int duration_ms = 500;    // Note duration in ms
  float velocity = 1.0f;    // 0.0 to 1.0
};

struct SongMetadata {
  std::string title;
  std::string description;
  float bpm = 120.0f;
  int beats_per_bar = 4;
  int note_per_beat = 4;
};

// Sanitizes note_per_beat by clamping [1, 32] and rounding to nearest power of 2.
int SanitizeNotePerBeat(int value);

struct Track {
  int id = 0;
  std::string name;
  const Instrument* instrument = nullptr;
  Clef clef = Clef::TrebleAndBass;
  float volume = 0.8f;  // 0.0 to 1.0
  bool muted = false;
  bool soloed = false;
  bool hidden = false;
  uint32 color = 0xFF4F46E5;  // ARGB color format
  std::vector<NoteEvent> notes;
};

class TrackManager {
 public:
  TrackManager();

  // Track manipulation
  Track* AddTrack(const std::string& name, const Instrument* instrument,
                  uint32 color);
  bool DeleteTrack(int track_id);
  bool MoveTrackUp(int track_id);
  bool MoveTrackDown(int track_id);
  bool MoveTrackToIndex(int track_id, int target_index);
  void ClearTrackNotes(int track_id);
  Track* GetTrack(int track_id);
  const Track* GetTrack(int track_id) const;
  std::vector<Track>& GetTracks();
  const std::vector<Track>& GetTracks() const;

  bool IsGlobalSoloActive() const { return global_solo_active_; }
  void SetGlobalSoloActive(bool active) { global_solo_active_ = active; }
  void ToggleGlobalSolo() { global_solo_active_ = !global_solo_active_; }
  bool HasAnySoloTrack() const;

  int GetActiveTrackId() const { return active_track_id_; }
  void SetActiveTrackId(int id) { active_track_id_ = id; }
  Track* GetActiveTrack();

  void ClearAllTracks();

  // BPM & Tick conversions
  float GetBpm() const { return bpm_; }
  void SetBpm(float bpm);
  static int TicksToMs(int ticks, float bpm);
  static int MsToTicks(int ms, float bpm);
  int TicksToMs(int ticks) const { return TicksToMs(ticks, bpm_); }
  int MsToTicks(int ms) const { return MsToTicks(ms, bpm_); }

  // Canvas, Note, & Snap Math Helpers
  static int SnapTick(int tick, int snap_ticks);
  static int GetKeyIndexAtX(float x, float width);
  static int GetTimeMsAtY(float y, float height, int view_start_ms,
                          float effective_speed);
  static double KeyIndexToFrequency(int key_index);
  static std::string KeyIndexToNoteName(int key_index);

  // Collision checking & note placement
  bool CanPlaceNote(int track_id, int key_index, int start_tick,
                    int duration_ticks, int ignore_note_index = -1) const;

  // Sync note ms values with ticks and current BPM
  void SyncTrackNotesMs(Track& track);

  // Playback & Recording controls
  bool IsPlaying() const { return is_playing_; }
  bool IsRecording() const { return is_recording_; }
  int GetCurrentTimeMs() const { return current_time_ms_; }
  int GetSongStartMs() const;
  int GetSongDurationMs() const;

  void StartPlay();
  void StartRecord();
  void Stop();
  void Seek(int time_ms);

  void SetOnRecordFinishedCallback(
      std::function<void(int track_id, const std::vector<NoteEvent>& old_notes)>
          cb) {
    on_record_finished_cb_ = std::move(cb);
  }

  // Live recording input
  void OnNotePressed(int key_index);
  void OnNoteReleased(int key_index);

  // Timeline update step (called on timer loop)
  void Tick(
      int elapsed_ms,
      const std::function<void(int key_index, const Instrument* instrument,
                               float volume, float duration_seconds)>&
          trigger_sound_cb);

  // Returns active keys currently pressed / playing for visual highlight
  std::map<int, uint32> GetActiveKeyHighlights() const;

 private:
  std::vector<Track> tracks_;
  int next_track_id_ = 1;
  int active_track_id_ = 0;
  float bpm_ = 120.0f;

  bool is_playing_ = false;
  bool is_recording_ = false;
  int current_time_ms_ = 0;
  bool global_solo_active_ = false;

  // Key pressed start times during live recording: key_index -> start_time_ms
  std::map<int, int> active_recording_keys_;

  // Last triggered note index to prevent duplicate note triggers: (track_id,
  // note_index)
  std::map<std::pair<int, size_t>, bool> triggered_notes_;

  std::vector<NoteEvent> pre_recording_notes_;
  int recording_track_id_ = 0;
  std::function<void(int track_id, const std::vector<NoteEvent>& old_notes)>
      on_record_finished_cb_;
};

// Preset palette of distinct track colors
uint32 GetTrackPresetColor(int index);
