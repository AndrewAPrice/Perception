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

#include "metronome.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

#include "perception/audio.h"

namespace {

// Frequency in Hz for downbeat major metronome click.
constexpr double kMajorTickFreqHz = 1200.0;
// Duration in seconds for downbeat major metronome click.
constexpr double kMajorTickDurationSec = 0.030;
// Volume scale for downbeat major metronome click.
constexpr double kMajorTickVolume = 28000.0;

// Frequency in Hz for minor beat metronome click.
constexpr double kMinorTickFreqHz = 800.0;
// Duration in seconds for minor beat metronome click.
constexpr double kMinorTickDurationSec = 0.025;
// Volume scale for minor beat metronome click.
constexpr double kMinorTickVolume = 18000.0;

std::shared_ptr<::perception::SharedMemory> CreateTickBuffer(
    int sample_rate, double frequency, double duration_seconds,
    double volume_scale) {
  int total_samples = static_cast<int>(sample_rate * duration_seconds);
  if (total_samples <= 0) return nullptr;
  size_t buffer_bytes = total_samples * sizeof(int16);

  auto shared_mem = ::perception::SharedMemory::FromSize(buffer_bytes, 0);
  if (!shared_mem || **shared_mem == nullptr) return nullptr;

  int16* samples = reinterpret_cast<int16*>(**shared_mem);
  double dt = 1.0 / sample_rate;
  for (int i = 0; i < total_samples; ++i) {
    double t = i * dt;
    double env = std::exp(-t / 0.005);
    double val =
        std::sin(2.0 * std::numbers::pi * frequency * t) * env * volume_scale;
    samples[i] = static_cast<int16>(std::clamp(val, -32767.0, 32767.0));
  }
  return shared_mem;
}

}  // namespace

Metronome::Metronome() = default;

void Metronome::Initialize(int sample_rate) {
  if (sample_rate <= 0) sample_rate = 48000;
  sample_rate_ = sample_rate;

  // Major tick downbeat click
  major_tick_buffer_ = CreateTickBuffer(
      sample_rate_, kMajorTickFreqHz, kMajorTickDurationSec, kMajorTickVolume);

  // Minor tick beat click
  minor_tick_buffer_ = CreateTickBuffer(
      sample_rate_, kMinorTickFreqHz, kMinorTickDurationSec, kMinorTickVolume);
}

void Metronome::SetEnabled(bool enabled) {
  if (is_enabled_ == enabled) return;
  is_enabled_ = enabled;
  Reset();
}

void Metronome::Reset() {
  last_beat_index_ = -1;
  live_jam_time_ms_ = 0.0;
  live_jam_beat_index_ = 0;
}

bool Metronome::IsMajorTick(int beat_index, int beats_per_bar) {
  if (beats_per_bar <= 0) beats_per_bar = 4;
  return (beat_index % beats_per_bar) == 0;
}

void Metronome::PlayTick(bool is_major) {
  auto buffer = is_major ? major_tick_buffer_ : minor_tick_buffer_;
  if (!buffer) return;
  ::perception::PlayAudio(buffer, 1.0f, false, sample_rate_, 1, 16);
}

void Metronome::Tick(int elapsed_ms, bool is_playing_or_recording,
                     int current_time_ms, float bpm, int beats_per_bar) {
  if (!is_enabled_) return;

  if (bpm <= 0.0f) bpm = 120.0f;
  if (beats_per_bar <= 0) beats_per_bar = 4;
  double ms_per_beat = 60000.0 / bpm;

  if (is_playing_or_recording) {
    if (current_time_ms < 0) return;
    int beat_index = static_cast<int>(current_time_ms / ms_per_beat);
    if (beat_index != last_beat_index_) {
      bool is_major = IsMajorTick(beat_index, beats_per_bar);
      PlayTick(is_major);
      last_beat_index_ = beat_index;
    }
  } else {
    // Live jam mode
    live_jam_time_ms_ += elapsed_ms;
    if (last_beat_index_ == -1) {
      // First tick upon starting live jam mode
      last_beat_index_ = 0;
      live_jam_beat_index_ = 0;
      live_jam_time_ms_ = 0.0;
      bool is_major = IsMajorTick(0, beats_per_bar);
      PlayTick(is_major);
    } else {
      while (live_jam_time_ms_ >= ms_per_beat) {
        live_jam_time_ms_ -= ms_per_beat;
        live_jam_beat_index_++;
        bool is_major = IsMajorTick(live_jam_beat_index_, beats_per_bar);
        PlayTick(is_major);
      }
    }
  }
}
