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

struct SaxophoneImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
    // Maximum number of overtone harmonics calculated for saxophone.
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
        if (h_freq > 16000.0) break;

        double sax_formant = FormantResonance(h_freq, 1800.0, 450.0, 1.8);
        double hf_damp = HighFrequencyDamping(h_freq, 5500.0);

        double amp = (1.0 / std::pow(h, 0.95)) * sax_formant * hf_damp;
        cache.weights[cache.num_harmonics++] = amp;
      }
    }

    SynthesizeVibratoAdditiveBlock(freq, t_start, dt, duration_seconds, 5.0,
                                   0.005, 0.1, 0.2, cache.weights,
                                   cache.num_harmonics, out_samples, count);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double t = t_start + i * dt;
      double env = SineAttackEnvelope(t, 0.03);
      out_samples[i] *= env * 0.32;
    }
  }
};

auto _ = RegisterInstrumentSpec<SaxophoneImpl>(
    "Alto Saxophone", "Winds & Brass", 0.14, false, 5.0, 25);

}  // namespace
}  // namespace instruments
