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

#include "perception/shared_memory.h"

class Metronome {
 public:
  Metronome();

  // Pre-synthesizes audio buffers for major and minor ticks.
  void Initialize(int sample_rate = 48000);

  // Toggles the metronome enabled state.
  void SetEnabled(bool enabled);
  bool IsEnabled() const { return is_enabled_; }

  // Resets beat tracking state (e.g., when seeking or stopping/starting playback).
  void Reset();

  // Called on each timer tick (~30ms loop).
  // Calculates whether a major or minor beat tick should be played.
  void Tick(int elapsed_ms, bool is_playing_or_recording, int current_time_ms,
            float bpm, int beats_per_bar);

  // Helper method to determine tick type for beat indices.
  static bool IsMajorTick(int beat_index, int beats_per_bar);

 private:
  void PlayTick(bool is_major);

  bool is_enabled_ = false;
  int sample_rate_ = 48000;
  std::shared_ptr<::perception::SharedMemory> major_tick_buffer_;
  std::shared_ptr<::perception::SharedMemory> minor_tick_buffer_;

  int last_beat_index_ = -1;
  double live_jam_time_ms_ = 0.0;
  int live_jam_beat_index_ = 0;
};
