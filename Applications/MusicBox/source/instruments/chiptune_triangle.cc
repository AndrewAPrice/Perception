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

#include "instruments.h"

namespace instruments {
namespace {

struct ChiptuneTriangleImpl {
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
      double attack = std::min(1.0, t / 0.002);
      double decay = std::exp(-1.5 * t / duration_seconds);
      double env = attack * decay;
      double phase = std::fmod(freq * t, 1.0);
      double tri = (phase < 0.5) ? (4.0 * phase - 1.0) : (3.0 - 4.0 * phase);
      out_samples[i] = env * tri * 0.8;
    }
  }
};

auto _ = RegisterInstrumentSpec<ChiptuneTriangleImpl>(
    "Chiptune (8-Bit Bass)", "Synthesizers & Electronic", 0.04, false, 6.0, 31);

}  // namespace
}  // namespace instruments
