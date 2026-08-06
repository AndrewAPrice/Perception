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

struct PipeOrganImpl {
  static void SynthesizeBlock(double freq, double t_start, double dt,
                              double duration_seconds, double* out_samples,
                              size_t count) {
    double phase16[256];
    double phase8[256];
    double phase4[256];
    double phase2[256];
    double sin16[256];
    double sin8[256];
    double sin4[256];
    double sin2[256];

    for (size_t chunk_start = 0; chunk_start < count; chunk_start += 256) {
      size_t chunk_size = std::min<size_t>(256, count - chunk_start);

      for (size_t i = 0; i < chunk_size; ++i) {
        double t = t_start + (chunk_start + i) * dt;
        double base_phase = 2.0 * std::numbers::pi * freq * t;
        phase16[i] = 0.5 * base_phase;
        phase8[i] = 1.0 * base_phase;
        phase4[i] = 2.0 * base_phase;
        phase2[i] = 4.0 * base_phase;
      }

      FastSinBlock(phase16, sin16, chunk_size);
      FastSinBlock(phase8, sin8, chunk_size);
      FastSinBlock(phase4, sin4, chunk_size);
      FastSinBlock(phase2, sin2, chunk_size);

#if defined(__clang__)
#pragma clang loop vectorize(enable)
#elif defined(__GNUG__)
#pragma GCC ivdep
#endif
      for (size_t i = 0; i < chunk_size; ++i) {
        double t = t_start + (chunk_start + i) * dt;
        double attack = std::min(1.0, t / 0.02);
        double release = (t > duration_seconds - 0.05)
                             ? std::max(0.0, (duration_seconds - t) / 0.05)
                             : 1.0;
        double env = attack * release;
        double r16 = 0.4 * sin16[i];
        double r8 = 1.0 * sin8[i];
        double r4 = 0.6 * sin4[i];
        double r2 = 0.3 * sin2[i];
        out_samples[chunk_start + i] = env * (r16 + r8 + r4 + r2) * 0.4;
      }
    }
  }
};

auto _ = RegisterInstrumentSpec<PipeOrganImpl>(
    "Pipe Organ", "Keyboards", 0.25, false, 6.0, 4);

}  // namespace
}  // namespace instruments
