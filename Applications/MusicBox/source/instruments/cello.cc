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

#include <algorithm>
#include <cmath>
#include <numbers>

#include "instruments.h"
#include "synth_helpers.h"

namespace instruments {
namespace {

struct CelloImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
    // Maximum number of overtone harmonics calculated for cello.
    constexpr int kMaxHarmonics = 14;
    struct HarmonicCache {
      double freq = -1.0;
      int num_harmonics = 0;
      double weights[kMaxHarmonics];
    };
    thread_local HarmonicCache cache;

    if (freq != cache.freq) {
      cache.freq = freq;
      cache.num_harmonics = 0;
      for (int h = 1; h <= kMaxHarmonics; ++h) {
        double h_freq = h * freq;
        if (h_freq > 14000.0) break;

        double amp = 1.0 / std::pow(h, 1.15);

        double air_res = FormantResonance(h_freq, 110.0, 40.0, 1.5);
        double wood_res = FormantResonance(h_freq, 220.0, 70.0, 1.8);
        double body_hill = FormantResonance(h_freq, 800.0, 250.0, 0.8);
        double hf_damp = HighFrequencyDamping(h_freq, 3200.0);

        double body_filter = air_res * wood_res * body_hill * hf_damp;
        cache.weights[cache.num_harmonics++] = amp * body_filter;
      }
    }

    SynthesizeVibratoAdditiveBlock(freq, t_start, dt, duration_seconds, 4.8,
                                   0.0045, 0.15, 0.3, cache.weights,
                                   cache.num_harmonics, out_samples, count);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double t = t_start + i * dt;
      double env = SineAttackEnvelope(t, 0.08);
      out_samples[i] *= env * 0.4;
    }
  }
};

auto _ = RegisterInstrumentSpec<CelloImpl>(
    "Cello / Low Strings", "Strings & Plucked", 0.28, false, 5.0, 8);

}  // namespace
}  // namespace instruments
