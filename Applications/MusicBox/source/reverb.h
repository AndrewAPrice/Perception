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

#include <cstddef>
#include <cstdint>
#include <vector>

class Reverb {
 public:
  Reverb();

  void SetSampleRate(int sample_rate);

  void SetEnabled(bool enabled) { enabled_ = enabled; }
  bool IsEnabled() const { return enabled_; }

  void SetMix(float mix);        // 0.0 (Dry) to 1.0 (Wet)
  float GetMix() const { return wet_mix_; }

  void SetRoomSize(float size);  // 0.0 (Small room) to 1.0 (Cathedral)
  float GetRoomSize() const { return room_size_; }

  void SetDamping(float damp);   // 0.0 (Bright) to 1.0 (Warm/Damped)
  float GetDamping() const { return damping_; }

  void ResetBuffers();

  // Process PCM int16 buffer in-place.
  void Process(int16_t* buffer, size_t num_frames);

  // Process floating point double buffer in-place.
  void Process(double* buffer, size_t num_frames);

 private:
  struct CombFilter {
    std::vector<float> buffer;
    size_t buf_size = 0;
    size_t buf_idx = 0;
    float feedback = 0.5f;
    float filter_store = 0.0f;
    float damp1 = 0.5f;
    float damp2 = 0.5f;

    float Process(float input) {
      if (buf_size == 0) return input;
      float output = buffer[buf_idx];
      filter_store = (output * damp2) + (filter_store * damp1);
      buffer[buf_idx] = input + (filter_store * feedback);
      buf_idx = (buf_idx + 1) % buf_size;
      return output;
    }
  };

  struct AllPassFilter {
    std::vector<float> buffer;
    size_t buf_size = 0;
    size_t buf_idx = 0;
    float feedback = 0.5f;

    float Process(float input) {
      if (buf_size == 0) return input;
      float buf_out = buffer[buf_idx];
      float output = -input + buf_out;
      buffer[buf_idx] = input + (buf_out * feedback);
      buf_idx = (buf_idx + 1) % buf_size;
      return output;
    }
  };

  void UpdateParameters();

  bool enabled_ = true;
  int sample_rate_ = 48000;

  float wet_mix_ = 0.25f;
  float dry_mix_ = 0.90f;
  float room_size_ = 0.75f;
  float damping_ = 0.40f;

  CombFilter comb_filters_[8];
  AllPassFilter allpass_filters_[4];
};

// Global Reverb instance accessor and helper functions
Reverb& GetGlobalReverb();
void SetReverbEnabled(bool enabled);
bool IsReverbEnabled();
void SetReverbMix(float mix);
float GetReverbMix();
void SetReverbRoomSize(float room_size);
float GetReverbRoomSize();
void SetReverbDamping(float damping);
float GetReverbDamping();
