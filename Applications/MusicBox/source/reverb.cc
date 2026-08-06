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

#include "reverb.h"

#include <algorithm>
#include <cmath>

namespace {

// Freeverb comb filter delay line buffer sizes in samples at 44.1 kHz base rate.
constexpr int kCombTuning[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
// Freeverb all-pass filter delay line buffer sizes in samples at 44.1 kHz base rate.
constexpr int kAllPassTuning[4] = {556, 441, 341, 225};

}  // namespace

Reverb::Reverb() { SetSampleRate(48000); }

void Reverb::SetSampleRate(int sample_rate) {
  if (sample_rate <= 0) return;
  sample_rate_ = sample_rate;
  double scale = static_cast<double>(sample_rate) / 44100.0;

  for (int i = 0; i < 8; ++i) {
    comb_filters_[i].buf_size = static_cast<size_t>(kCombTuning[i] * scale);
    if (comb_filters_[i].buf_size == 0) comb_filters_[i].buf_size = 1;
    comb_filters_[i].buffer.assign(comb_filters_[i].buf_size, 0.0f);
    comb_filters_[i].buf_idx = 0;
    comb_filters_[i].filter_store = 0.0f;
  }

  for (int i = 0; i < 4; ++i) {
    allpass_filters_[i].buf_size = static_cast<size_t>(kAllPassTuning[i] * scale);
    if (allpass_filters_[i].buf_size == 0) allpass_filters_[i].buf_size = 1;
    allpass_filters_[i].buffer.assign(allpass_filters_[i].buf_size, 0.0f);
    allpass_filters_[i].buf_idx = 0;
    allpass_filters_[i].feedback = 0.5f;
  }

  UpdateParameters();
}

void Reverb::SetMix(float mix) {
  wet_mix_ = std::clamp(mix, 0.0f, 1.0f);
  dry_mix_ = 1.0f - (wet_mix_ * 0.4f);
}

void Reverb::SetRoomSize(float size) {
  room_size_ = std::clamp(size, 0.0f, 1.0f);
  UpdateParameters();
}

void Reverb::SetDamping(float damp) {
  damping_ = std::clamp(damp, 0.0f, 1.0f);
  UpdateParameters();
}

void Reverb::UpdateParameters() {
  float fb = 0.70f + (room_size_ * 0.28f);
  float d1 = damping_ * 0.4f;
  float d2 = 1.0f - d1;

  for (int i = 0; i < 8; ++i) {
    comb_filters_[i].feedback = fb;
    comb_filters_[i].damp1 = d1;
    comb_filters_[i].damp2 = d2;
  }
}

void Reverb::ResetBuffers() {
  for (int i = 0; i < 8; ++i) {
    std::fill(comb_filters_[i].buffer.begin(), comb_filters_[i].buffer.end(), 0.0f);
    comb_filters_[i].filter_store = 0.0f;
    comb_filters_[i].buf_idx = 0;
  }
  for (int i = 0; i < 4; ++i) {
    std::fill(allpass_filters_[i].buffer.begin(), allpass_filters_[i].buffer.end(), 0.0f);
    allpass_filters_[i].buf_idx = 0;
  }
}

void Reverb::Process(int16_t* buffer, size_t num_frames) {
  if (!enabled_ || !buffer) return;

  for (size_t f = 0; f < num_frames; ++f) {
    float input = static_cast<float>(buffer[f]) / 32768.0f;
    float out_comb = 0.0f;
    for (int i = 0; i < 8; ++i) {
      out_comb += comb_filters_[i].Process(input);
    }
    float out_allpass = out_comb;
    for (int i = 0; i < 4; ++i) {
      out_allpass = allpass_filters_[i].Process(out_allpass);
    }
    float mixed = (input * dry_mix_) + (out_allpass * wet_mix_ * 0.25f);
    buffer[f] = static_cast<int16_t>(
        std::clamp(mixed * 32767.0f, -32767.0f, 32767.0f));
  }
}

void Reverb::Process(double* buffer, size_t num_frames) {
  if (!enabled_ || !buffer) return;

  for (size_t f = 0; f < num_frames; ++f) {
    float input = static_cast<float>(buffer[f]);
    float out_comb = 0.0f;
    for (int i = 0; i < 8; ++i) {
      out_comb += comb_filters_[i].Process(input);
    }
    float out_allpass = out_comb;
    for (int i = 0; i < 4; ++i) {
      out_allpass = allpass_filters_[i].Process(out_allpass);
    }
    double wet = out_allpass * wet_mix_ * 0.25;
    buffer[f] = (buffer[f] * dry_mix_) + wet;
  }
}




namespace {
Reverb g_global_reverb;
}  // namespace

Reverb& GetGlobalReverb() { return g_global_reverb; }
void SetReverbEnabled(bool enabled) { g_global_reverb.SetEnabled(enabled); }
bool IsReverbEnabled() { return g_global_reverb.IsEnabled(); }
void SetReverbMix(float mix) { g_global_reverb.SetMix(mix); }
float GetReverbMix() { return g_global_reverb.GetMix(); }
void SetReverbRoomSize(float size) { g_global_reverb.SetRoomSize(size); }
float GetReverbRoomSize() { return g_global_reverb.GetRoomSize(); }
void SetReverbDamping(float damping) { g_global_reverb.SetDamping(damping); }
float GetReverbDamping() { return g_global_reverb.GetDamping(); }
