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

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace instruments {

// Precalculated sine wave lookup table for fast synthesis.
class SineTable {
 public:
  // Size of the precalculated sine lookup table.
  static constexpr int kTableSize = 4096;

  SineTable() {
    for (int i = 0; i < kTableSize; ++i) {
      table_[i] = std::sin((2.0 * std::numbers::pi * i) / kTableSize);
    }
  }

  inline double Sin(double radians) const {
    double normalized = radians * (kTableSize / (2.0 * std::numbers::pi));
    int idx0 = static_cast<int>(std::floor(normalized)) & (kTableSize - 1);
    int idx1 = (idx0 + 1) & (kTableSize - 1);
    double frac = normalized - std::floor(normalized);
    return table_[idx0] + frac * (table_[idx1] - table_[idx0]);
  }

  inline double Cos(double radians) const {
    return Sin(radians + 0.5 * std::numbers::pi);
  }

  inline void SinBlock(const double* radians, double* out, size_t count) const {
    double factor = kTableSize / (2.0 * std::numbers::pi);
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double normalized = radians[i] * factor;
      int idx0 = static_cast<int>(std::floor(normalized)) & (kTableSize - 1);
      int idx1 = (idx0 + 1) & (kTableSize - 1);
      double frac = normalized - std::floor(normalized);
      out[i] = table_[idx0] + frac * (table_[idx1] - table_[idx0]);
    }
  }

 private:
  double table_[kTableSize];
};

inline const SineTable& GetSineTable() {
  static const SineTable kSineTable;
  return kSineTable;
}

inline double FastSin(double radians) { return GetSineTable().Sin(radians); }

inline double FastCos(double radians) { return GetSineTable().Cos(radians); }

inline void FastSinBlock(const double* radians, double* out, size_t count) {
  GetSineTable().SinBlock(radians, out, count);
}

inline double ComputeVibratoOnset(double t, double delay_seconds,
                                  double ramp_seconds) {
  return std::clamp((t - delay_seconds) / ramp_seconds, 0.0, 1.0);
}

inline double ComputeVibratoPhase(double freq, double t, double vibrato_rate,
                                  double vibrato_depth) {
  if (vibrato_depth == 0.0 || vibrato_rate == 0.0) {
    return 2.0 * std::numbers::pi * freq * t;
  }
  double vibrato_phase_offset =
      (freq * vibrato_depth / vibrato_rate) *
      FastCos(2.0 * std::numbers::pi * vibrato_rate * t);
  return 2.0 * std::numbers::pi * freq * t - vibrato_phase_offset;
}

inline double SineAttackEnvelope(double t, double attack_seconds) {
  return FastSin(0.5 * std::numbers::pi *
                 std::clamp(t / attack_seconds, 0.0, 1.0));
}

inline double LinearAttackEnvelope(double t, double attack_seconds) {
  return std::min(1.0, t / attack_seconds);
}

inline double ExponentialDecayEnvelope(double t, double duration_seconds,
                                       double decay_rate) {
  return std::exp(-decay_rate * t / duration_seconds);
}

inline double FormantResonance(double h_freq, double center_freq,
                               double bandwidth, double peak_boost) {
  return 1.0 +
         peak_boost / (1.0 + std::pow((h_freq - center_freq) / bandwidth, 2.0));
}

inline double HighFrequencyDamping(double h_freq, double cutoff_freq) {
  return std::exp(-std::pow(h_freq / cutoff_freq, 2.0));
}

inline double DeterministicBreathNoise(double t) {
  return FastSin(12345.67 * t) * FastSin(98765.43 * t);
}

inline void SynthesizeVibratoAdditiveBlock(
    double freq, double t_start, double dt, double duration_seconds,
    double vibrato_rate, double vibrato_depth, double vibrato_delay,
    double vibrato_ramp, const double* harmonic_weights, int num_harmonics,
    double* out_samples, size_t count) {
  std::fill_n(out_samples, count, 0.0);

  double phase_buf[256];
  double sin_buf[256];

  for (size_t chunk_start = 0; chunk_start < count; chunk_start += 256) {
    size_t chunk_size = std::min<size_t>(256, count - chunk_start);

    for (int h_idx = 0; h_idx < num_harmonics; ++h_idx) {
      int h = h_idx + 1;
      double w = harmonic_weights[h_idx];

      for (size_t i = 0; i < chunk_size; ++i) {
        double t = t_start + (chunk_start + i) * dt;
        double vibrato_onset =
            ComputeVibratoOnset(t, vibrato_delay, vibrato_ramp);
        double depth = vibrato_depth * vibrato_onset;
        double base_phase = ComputeVibratoPhase(freq, t, vibrato_rate, depth);
        phase_buf[i] = h * base_phase;
      }

      FastSinBlock(phase_buf, sin_buf, chunk_size);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
      for (size_t i = 0; i < chunk_size; ++i) {
        out_samples[chunk_start + i] += w * sin_buf[i];
      }
    }
  }
}

}  // namespace instruments
