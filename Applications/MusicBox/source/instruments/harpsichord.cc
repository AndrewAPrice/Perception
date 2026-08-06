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

struct HarpsichordImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
    for (size_t i = 0; i < count; ++i) {
      double t = t_start + i * dt;
      double attack = std::min(1.0, t / 0.001);
      double decay = std::exp(-3.5 * t / duration_seconds);
      double env = attack * decay;
      double phase = std::fmod(freq * t, 1.0);
      double pluck = (phase < 0.2) ? 1.0 : -0.25;
      double h2 = 0.6 * FastSin(2.0 * std::numbers::pi * 2.0 * freq * t);
      out_samples[i] = env * (pluck + h2) * 0.6;
    }
  }
};

auto _ = RegisterInstrumentSpec<HarpsichordImpl>(
    "Harpsichord", "Keyboards", 0.10, false, 2.5, 2);

}  // namespace
}  // namespace instruments
