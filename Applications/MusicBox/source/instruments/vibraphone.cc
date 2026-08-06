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

struct VibraphoneImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
    double weights[4] = {1.0, 0.0, 0.0, 0.3};
    SynthesizeVibratoAdditiveBlock(freq, t_start, dt, duration_seconds, 0.0,
                                   0.0, 0.0, 0.0, weights, 4, out_samples,
                                   count);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double t = t_start + i * dt;
      double attack = std::min(1.0, t / 0.005);
      double decay = std::exp(-1.5 * t / duration_seconds);
      double env = attack * decay;
      double tremolo = 1.0 + 0.3 * FastSin(2.0 * std::numbers::pi * 5.0 * t);
      out_samples[i] *= env * tremolo * 0.7;
    }
  }
};

auto _ = RegisterInstrumentSpec<VibraphoneImpl>(
    "Vibraphone", "Chromatic Percussion & Bells", 0.40, true, 3.5, 20);

}  // namespace
}  // namespace instruments
