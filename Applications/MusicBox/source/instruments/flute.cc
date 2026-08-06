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
#include <cstdlib>
#include <numbers>

#include "instruments.h"
#include "synth_helpers.h"

namespace instruments {
namespace {

struct FluteImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
    double weights[3] = {1.0, 0.15, 0.05};
    SynthesizeVibratoAdditiveBlock(freq, t_start, dt, duration_seconds, 5.2,
                                   0.0045, 0.1, 0.2, weights, 3, out_samples,
                                   count);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double t = t_start + i * dt;
      double env = SineAttackEnvelope(t, 0.04);
      double breath = 0.025 * DeterministicBreathNoise(t);
      out_samples[i] = env * (out_samples[i] + breath) * 0.65;
    }
  }
};

auto _ = RegisterInstrumentSpec<FluteImpl>(
    "Concert Flute", "Winds & Brass", 0.14, false, 5.0, 21);

}  // namespace
}  // namespace instruments
