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

#include <string>
#include <string_view>
#include <vector>

using SynthesizeSampleFn = double (*)(double freq, double t,
                                      double duration_seconds);
using SynthesizeBlockFn = void (*)(double freq, double t_start, double dt,
                                   double duration_seconds, double* out_samples,
                                   size_t count);

enum class Clef {
  TrebleAndBass = 0,
  Alto,
  Tenor
};

struct CategorizedInstrumentOption {
  std::string text;
  bool is_category = false;
};

struct Instrument {
  // Human-readable display name (e.g. "Acoustic Piano").
  std::string name;
  // Grouping category for UI display (e.g. "Keyboards", "Winds & Brass").
  std::string category;
  // Pointer to sample synthesis function returning audio amplitude (-1.0
  // to 1.0).
  SynthesizeSampleFn synthesize_sample = nullptr;
  // Duration in seconds for the note envelope to fade out upon release.
  double release_duration_seconds = 0.15;
  // If true, NoteOff events are ignored and the note rings out naturally (e.g.
  // Harp, Bells).
  bool ignore_note_off = false;
  // Maximum duration in seconds a held note will sustain before automatically
  // ending.
  double max_sustain_seconds = 4.0;
  // Integer priority used to sort instruments deterministically in UI lists.
  int sort_order = 0;
  // Optional vectorized/block synthesis function pointer.
  SynthesizeBlockFn synthesize_block = nullptr;
  // Default clef used when selecting this instrument.
  Clef default_clef = Clef::TrebleAndBass;
};

// Generic adapter creating a single-sample callback from a block synthesizer
template <SynthesizeBlockFn BlockFn>
double SingleSampleAdapter(double freq, double t, double duration_seconds) {
  double sample = 0.0;
  BlockFn(freq, t, 0.0, duration_seconds, &sample, 1);
  return sample;
}

// Synthesizes a block of samples for an instrument using synthesize_block if
// available, falling back to synthesize_sample.
inline void SynthesizeInstrumentBlock(const Instrument* inst, double freq,
                                      double t_start, double dt,
                                      double duration_seconds,
                                      double* out_samples, size_t count) {
  if (inst && inst->synthesize_block) {
    inst->synthesize_block(freq, t_start, dt, duration_seconds, out_samples,
                           count);
  } else if (inst && inst->synthesize_sample) {
    for (size_t i = 0; i < count; ++i) {
      out_samples[i] =
          inst->synthesize_sample(freq, t_start + i * dt, duration_seconds);
    }
  } else {
    std::fill_n(out_samples, count, 0.0);
  }
}

// Returns a reference to the global list of registered instruments.
const std::vector<const Instrument*>& GetInstruments();

// Registers an instrument into the global buffer during program initialization.
void RegisterInstrument(const Instrument* instrument);

// Template registration function that auto-binds synthesize_sample and synthesize_block
template <typename Impl>
const Instrument* RegisterInstrumentSpec(
    std::string_view name, std::string_view category, double release_dur,
    bool ignore_off, double max_sustain, int sort_order,
    Clef default_clef = Clef::TrebleAndBass) {
  static const Instrument inst = {
      .name = std::string(name),
      .category = std::string(category),
      .synthesize_sample = &SingleSampleAdapter<Impl::SynthesizeBlock>,
      .release_duration_seconds = release_dur,
      .ignore_note_off = ignore_off,
      .max_sustain_seconds = max_sustain,
      .sort_order = sort_order,
      .synthesize_block = &Impl::SynthesizeBlock,
      .default_clef = default_clef};
  RegisterInstrument(&inst);
  return &inst;
}

// Returns an instrument by its display name (or default if not found).
const Instrument* GetInstrument(std::string_view name);

// Returns the default instrument (e.g. Acoustic Piano).
const Instrument* GetDefaultInstrument();

// Returns a vector of all available instrument names.
std::vector<std::string> GetInstrumentNames();

// Returns instrument options formatted with category headers.
std::vector<CategorizedInstrumentOption> GetCategorizedInstrumentOptions();

// Returns the instrument associated with a ComboBox option index (or fallback if category header).
const Instrument* GetInstrumentFromOptionIndex(int option_index);

// Returns the ComboBox option index for a given instrument pointer.
int GetOptionIndexForInstrument(const Instrument* inst);
