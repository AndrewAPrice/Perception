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

struct SynthPadImpl {
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
      double attack = std::min(1.0, t / 0.25);
      double env = attack;
      double s1 = FastSin(2.0 * std::numbers::pi * freq * t);
      double s2 = FastSin(2.0 * std::numbers::pi * freq * 1.003 * t);
      double s3 = FastSin(2.0 * std::numbers::pi * freq * 0.997 * t);
      out_samples[i] = env * (s1 + s2 + s3) * 0.3;
    }
  }
};

auto _ = RegisterInstrumentSpec<SynthPadImpl>(
    "Warm Synth Pad", "Synthesizers & Electronic", 0.55, false, 6.0, 33);

}  // namespace
}  // namespace instruments
