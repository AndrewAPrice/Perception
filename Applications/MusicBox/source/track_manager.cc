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

#include <algorithm>
#include <cmath>

namespace {

// Valid power-of-two note division denominator values for time signatures.
constexpr int kValidNotePerBeatValues[] = {1, 2, 4, 8, 16, 32};

// Preset ARGB color values assigned to new tracks.
constexpr uint32 kPresetColors[] = {
    0xFF4F46E5,  // Indigo
    0xFF10B981,  // Emerald
    0xFFF59E0B,  // Amber
    0xFFEF4444,  // Rose Red
    0xFF8B5CF6,  // Purple
    0xFF06B6D4,  // Cyan
    0xFFEC4899,  // Pink
    0xFF84CC16   // Lime
};

// Number of available track preset colors.
constexpr int kNumPresetColors = sizeof(kPresetColors) / sizeof(uint32);

}  // namespace

int SanitizeNotePerBeat(int value) {
  if (value <= 1) return 1;
  if (value >= 32) return 32;
  int best = 1;
  int min_diff = std::abs(value - 1);
  for (int v : kValidNotePerBeatValues) {
    int diff = std::abs(value - v);
    if (diff < min_diff) {
      min_diff = diff;
      best = v;
    }
  }
  return best;
}

uint32 GetTrackPresetColor(int index) {
  return kPresetColors[index % kNumPresetColors];
}


TrackManager::TrackManager() {
  tracks_.reserve(32);
  // Add an initial default track
  AddTrack("Track 1", GetDefaultInstrument(), GetTrackPresetColor(0));
}

Track* TrackManager::AddTrack(const std::string& name,
                              const Instrument* instrument, uint32 color) {
  Track new_track;
  new_track.id = next_track_id_++;
  new_track.name = name;
  new_track.instrument = instrument ? instrument : GetDefaultInstrument();
  new_track.clef =
      new_track.instrument ? new_track.instrument->default_clef : Clef::TrebleAndBass;
  new_track.volume = 0.8f;
  new_track.muted = false;
  new_track.color = color;

  tracks_.push_back(new_track);
  if (active_track_id_ == 0) {
    active_track_id_ = new_track.id;
  }
  return &tracks_.back();
}

bool TrackManager::DeleteTrack(int track_id) {
  if (tracks_.size() <= 1) {
    // Keep at least one track
    return false;
  }
  auto it =
      std::find_if(tracks_.begin(), tracks_.end(),
                   [track_id](const Track& t) { return t.id == track_id; });
  if (it != tracks_.end()) {
    tracks_.erase(it);
    if (active_track_id_ == track_id) {
      active_track_id_ = tracks_.front().id;
    }
    return true;
  }
  return false;
}

bool TrackManager::MoveTrackUp(int track_id) {
  for (size_t i = 1; i < tracks_.size(); ++i) {
    if (tracks_[i].id == track_id) {
      std::swap(tracks_[i], tracks_[i - 1]);
      return true;
    }
  }
  return false;
}

bool TrackManager::MoveTrackDown(int track_id) {
  if (tracks_.empty()) return false;
  for (size_t i = 0; i + 1 < tracks_.size(); ++i) {
    if (tracks_[i].id == track_id) {
      std::swap(tracks_[i], tracks_[i + 1]);
      return true;
    }
  }
  return false;
}

bool TrackManager::MoveTrackToIndex(int track_id, int target_index) {
  if (tracks_.empty()) return false;
  target_index = std::clamp(target_index, 0, static_cast<int>(tracks_.size()) - 1);
  size_t curr_idx = tracks_.size();
  for (size_t i = 0; i < tracks_.size(); ++i) {
    if (tracks_[i].id == track_id) {
      curr_idx = i;
      break;
    }
  }
  if (curr_idx >= tracks_.size() || curr_idx == static_cast<size_t>(target_index)) return false;
  Track t = std::move(tracks_[curr_idx]);
  tracks_.erase(tracks_.begin() + curr_idx);
  tracks_.insert(tracks_.begin() + target_index, std::move(t));
  return true;
}

bool TrackManager::HasAnySoloTrack() const {
  for (const auto& track : tracks_) {
    if (track.soloed) return true;
  }
  return false;
}

void TrackManager::ClearTrackNotes(int track_id) {
  Track* t = GetTrack(track_id);
  if (t) {
    t->notes.clear();
  }
}

Track* TrackManager::GetTrack(int track_id) {
  for (auto& track : tracks_) {
    if (track.id == track_id) return &track;
  }
  return nullptr;
}

const Track* TrackManager::GetTrack(int track_id) const {
  for (const auto& track : tracks_) {
    if (track.id == track_id) return &track;
  }
  return nullptr;
}

std::vector<Track>& TrackManager::GetTracks() { return tracks_; }

const std::vector<Track>& TrackManager::GetTracks() const { return tracks_; }

Track* TrackManager::GetActiveTrack() { return GetTrack(active_track_id_); }

void TrackManager::SetBpm(float bpm) {
  if (bpm < 10.0f) bpm = 10.0f;
  if (bpm > 400.0f) bpm = 400.0f;
  bpm_ = bpm;
  for (auto& track : tracks_) {
    SyncTrackNotesMs(track);
  }
}

int TrackManager::TicksToMs(int ticks, float bpm) {
  if (bpm <= 0.0f) bpm = 120.0f;
  return static_cast<int>(std::round(ticks * (60000.0 / (bpm * 64.0))));
}

namespace {

const bool kIsBlackKey[12] = {false, true,  false, false, true,  false,
                              true,  false, false, true,  false, true};

const int kWhiteKeyIndexInOctave[12] = {0, 1, 1, 2, 3, 3, 4, 4, 5, 6, 6, 7};

int GetWhiteKeyCount(int key_index) {
  int octave = key_index / 12;
  int note = key_index % 12;
  return octave * 7 + kWhiteKeyIndexInOctave[note];
}

bool IsKeyBlack(int key_index) { return kIsBlackKey[key_index % 12]; }

}  // namespace

int TrackManager::MsToTicks(int ms, float bpm) {
  if (bpm <= 0.0f) bpm = 120.0f;
  return static_cast<int>(std::round(ms * (bpm * 64.0) / 60000.0));
}

int TrackManager::SnapTick(int tick, int snap_ticks) {
  if (snap_ticks <= 1) return std::max(0, tick);
  int snapped = static_cast<int>(
      std::round(static_cast<double>(tick) / snap_ticks) * snap_ticks);
  return std::max(0, snapped);
}

int TrackManager::GetKeyIndexAtX(float x, float w) {
  if (w <= 0.0f) return 0;
  float white_key_w = w / 52.0f;
  float black_key_w = white_key_w * 0.7f;

  for (int k = 0; k < 88; ++k) {
    if (!IsKeyBlack(k)) continue;
    int w_idx = GetWhiteKeyCount(k);
    float x_center = w_idx * white_key_w;
    float x_left = x_center - (black_key_w / 2.0f);
    if (x >= x_left && x <= x_left + black_key_w) {
      return k;
    }
  }

  int w_idx = static_cast<int>(x / white_key_w);
  w_idx = std::clamp(w_idx, 0, 51);

  for (int k = 0; k < 88; ++k) {
    if (!IsKeyBlack(k) && GetWhiteKeyCount(k) == w_idx) {
      return k;
    }
  }

  return 0;
}

int TrackManager::GetTimeMsAtY(float y, float h, int view_start_ms,
                                float eff_speed) {
  float dt = (h - 4.0f - y) / (eff_speed > 0.0f ? eff_speed : 0.12f);
  return view_start_ms + static_cast<int>(dt);
}

double TrackManager::KeyIndexToFrequency(int key_index) {
  return 440.0 * std::pow(2.0, (key_index - 48) / 12.0);
}

std::string TrackManager::KeyIndexToNoteName(int key_index) {
  static const char* kNoteNames[] = {"A",  "A#", "B", "C",  "C#", "D",
                                     "D#", "E",  "F", "F#", "G",  "G#"};
  if (key_index < 0 || key_index >= 88) return "";
  int note_in_octave = key_index % 12;
  int octave = (key_index + 9) / 12;
  return std::string(kNoteNames[note_in_octave]) + std::to_string(octave);
}

bool TrackManager::CanPlaceNote(int track_id, int key_index, int start_tick,
                                int duration_ticks, int ignore_note_index) const {
  if (duration_ticks <= 0) return false;
  if (key_index < 0 || key_index > 87) return false;
  if (start_tick < 0) return false;

  const Track* track = GetTrack(track_id);
  if (!track) return false;

  int end_tick = start_tick + duration_ticks;
  for (size_t i = 0; i < track->notes.size(); ++i) {
    if (static_cast<int>(i) == ignore_note_index) continue;
    const auto& note = track->notes[i];
    if (note.key_index == key_index) {
      int other_end = note.start_tick + note.duration_ticks;
      if (start_tick < other_end && end_tick > note.start_tick) {
        return false;
      }
    }
  }
  return true;
}

void TrackManager::SyncTrackNotesMs(Track& track) {
  for (auto& note : track.notes) {
    note.start_time_ms = TicksToMs(note.start_tick, bpm_);
    note.duration_ms = std::max(1, TicksToMs(note.duration_ticks, bpm_));
  }
}

void TrackManager::ClearAllTracks() {
  tracks_.clear();
  tracks_.reserve(32);
  next_track_id_ = 1;
  active_track_id_ = 0;
  active_recording_keys_.clear();
  triggered_notes_.clear();
  current_time_ms_ = 0;
  is_playing_ = false;
  is_recording_ = false;
}

int TrackManager::GetSongStartMs() const {
  int min_start = -1;
  for (const auto& track : tracks_) {
    for (const auto& note : track.notes) {
      if (min_start == -1 || note.start_time_ms < min_start) {
        min_start = note.start_time_ms;
      }
    }
  }
  return (min_start == -1) ? 0 : min_start;
}

int TrackManager::GetSongDurationMs() const {
  int max_duration = 0;
  for (const auto& track : tracks_) {
    const Instrument* inst =
        track.instrument ? track.instrument : GetDefaultInstrument();
    double release_sec = inst ? inst->release_duration_seconds : 0.15;
    int release_ms = static_cast<int>(std::round(release_sec * 1000.0));

    for (const auto& note : track.notes) {
      max_duration = std::max(
          max_duration, note.start_time_ms + note.duration_ms + release_ms);
    }
  }
  return std::max(max_duration, 1000);
}

void TrackManager::StartPlay() {
  is_playing_ = true;
  is_recording_ = false;
  triggered_notes_.clear();
}

void TrackManager::StartRecord() {
  is_playing_ = true;
  is_recording_ = true;
  active_recording_keys_.clear();
  triggered_notes_.clear();
  Track* active_track = GetActiveTrack();
  if (active_track) {
    pre_recording_notes_ = active_track->notes;
    recording_track_id_ = active_track->id;
  } else {
    pre_recording_notes_.clear();
    recording_track_id_ = 0;
  }
}

void TrackManager::Stop() {
  bool was_recording = is_recording_;
  // If recording, complete any unclosed notes
  if (is_recording_) {
    Track* active_track = GetActiveTrack();
    if (active_track) {
      for (const auto& [key, start_time] : active_recording_keys_) {
        int duration = std::max(150, current_time_ms_ - start_time);
        int start_tick = MsToTicks(start_time, bpm_);
        int dur_ticks = std::max(1, MsToTicks(duration, bpm_));
        active_track->notes.push_back(
            NoteEvent{.key_index = key,
                      .start_tick = start_tick,
                      .duration_ticks = dur_ticks,
                      .start_time_ms = start_time,
                      .duration_ms = duration,
                      .velocity = 1.0f});
      }
    }
  }

  is_playing_ = false;
  is_recording_ = false;
  active_recording_keys_.clear();
  triggered_notes_.clear();

  if (was_recording && recording_track_id_ != 0) {
    Track* active_track = GetTrack(recording_track_id_);
    if (active_track && active_track->notes.size() > pre_recording_notes_.size()) {
      if (on_record_finished_cb_) {
        on_record_finished_cb_(recording_track_id_, pre_recording_notes_);
      }
    }
  }
}

void TrackManager::Seek(int time_ms) {
  current_time_ms_ = std::max(0, time_ms);
  triggered_notes_.clear();
}

void TrackManager::OnNotePressed(int key_index) {
  if (is_recording_) {
    active_recording_keys_[key_index] = current_time_ms_;
  }
}

void TrackManager::OnNoteReleased(int key_index) {
  if (is_recording_) {
    auto it = active_recording_keys_.find(key_index);
    if (it != active_recording_keys_.end()) {
      int start_time = it->second;
      int duration = std::max(150, current_time_ms_ - start_time);
      int start_tick = MsToTicks(start_time, bpm_);
      int dur_ticks = std::max(1, MsToTicks(duration, bpm_));
      Track* active_track = GetActiveTrack();
      if (active_track) {
        active_track->notes.push_back(
            NoteEvent{.key_index = key_index,
                      .start_tick = start_tick,
                      .duration_ticks = dur_ticks,
                      .start_time_ms = start_time,
                      .duration_ms = duration,
                      .velocity = 1.0f});
      }
      active_recording_keys_.erase(it);
    }
  }
}

void TrackManager::Tick(
    int elapsed_ms,
    const std::function<void(int key_index, const Instrument* instrument,
                             float volume, float duration_seconds)>&
        trigger_sound_cb) {
  if (!is_playing_) return;

  int prev_time = current_time_ms_;
  current_time_ms_ += elapsed_ms;

  bool solo_active = global_solo_active_ || HasAnySoloTrack();

  for (const auto& track : tracks_) {
    if (track.muted) continue;
    if (solo_active && !track.soloed) continue;

    for (size_t i = 0; i < track.notes.size(); ++i) {
      const auto& note = track.notes[i];
      if (note.start_time_ms >= prev_time &&
          note.start_time_ms < current_time_ms_) {
        auto note_key = std::make_pair(track.id, i);
        if (!triggered_notes_[note_key]) {
          triggered_notes_[note_key] = true;
          if (trigger_sound_cb) {
            trigger_sound_cb(note.key_index, track.instrument,
                             track.volume * note.velocity,
                             note.duration_ms / 1000.0f);
          }
        }
      }
    }
  }

  // Stop playback if reached end of song during playback (when not recording)
  int song_duration_ms = GetSongDurationMs();
  if (!is_recording_ && current_time_ms_ >= song_duration_ms) {
    current_time_ms_ = song_duration_ms;
    Stop();
  }
}

std::map<int, uint32> TrackManager::GetActiveKeyHighlights() const {
  std::map<int, uint32> active_keys;
  if (!is_playing_) return active_keys;

  bool solo_active = global_solo_active_ || HasAnySoloTrack();

  for (const auto& track : tracks_) {
    if (track.muted) continue;
    if (solo_active && !track.soloed) continue;
    for (const auto& note : track.notes) {
      if (current_time_ms_ >= note.start_time_ms &&
          current_time_ms_ <= note.start_time_ms + note.duration_ms) {
        active_keys[note.key_index] = track.color;
      }
    }
  }
  return active_keys;
}
