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

#include "notation/notation_composition.h"

#include <algorithm>
#include <vector>

namespace notation {

namespace {

// In MusicBox composition timing, 1 Whole note is defined as 256 ticks
// (kTicksPerWholeNote = 256).
//
// note_per_beat specifies the denominator of the time signature (the note type
// that receives 1 beat), e.g.:
//   - 1 = Whole note (1 beat = 256 / 1 = 256 ticks)
//   - 2 = Half note (1 beat = 256 / 2 = 128 ticks)
//   - 4 = Quarter note (1 beat = 256 / 4 = 64 ticks)
//   - 8 = Eighth note (1 beat = 256 / 8 = 32 ticks)
//   - 16 = Sixteenth note (1 beat = 256 / 16 = 16 ticks)
//   - 32 = Thirty-second note (1 beat = 256 / 32 = 8 ticks)
//
// The duration of 1 beat in ticks is:
//   ticks_per_beat = kTicksPerWholeNote / note_per_beat
//
// Total ticks per bar (measure):
//   ticks_per_bar = beats_per_bar * ticks_per_beat
constexpr int kTicksPerWholeNote = 256;

struct SymbolDef {
  NoteSymbolType type;
  int base_ticks;
  int total_ticks;
  bool is_dotted;
};

// Supported note symbols ordered from largest to smallest duration.
constexpr SymbolDef kSymbols[] = {
    {NoteSymbolType::Maxima, 2048, 3072, true},
    {NoteSymbolType::Maxima, 2048, 2048, false},
    {NoteSymbolType::Longa, 1024, 1536, true},
    {NoteSymbolType::Longa, 1024, 1024, false},
    {NoteSymbolType::Breve, 512, 768, true},
    {NoteSymbolType::Breve, 512, 512, false},
    {NoteSymbolType::Whole, 256, 384, true},
    {NoteSymbolType::Whole, 256, 256, false},
    {NoteSymbolType::Half, 128, 192, true},
    {NoteSymbolType::Half, 128, 128, false},
    {NoteSymbolType::Quarter, 64, 96, true},
    {NoteSymbolType::Quarter, 64, 64, false},
    {NoteSymbolType::Eighth, 32, 48, true},
    {NoteSymbolType::Eighth, 32, 32, false},
    {NoteSymbolType::Sixteenth, 16, 24, true},
    {NoteSymbolType::Sixteenth, 16, 16, false},
    {NoteSymbolType::ThirtySecond, 8, 12, true},
    {NoteSymbolType::ThirtySecond, 8, 8, false},
    {NoteSymbolType::SixtyFourth, 4, 6, true},
    {NoteSymbolType::SixtyFourth, 4, 4, false},
    {NoteSymbolType::OneHundredTwentyEighth, 2, 3, true},
    {NoteSymbolType::OneHundredTwentyEighth, 2, 2, false},
    {NoteSymbolType::TwoHundredAndFiftySixth, 1, 1, false},
};

}  // namespace

void DecomposeNote(int start_tick, int duration_ticks, int beats_per_bar,
                   int note_per_beat, const OnSymbolCallback& on_symbol,
                   bool rest) {
  if (!on_symbol || duration_ticks <= 0) return;

  int ticks_per_beat = kTicksPerWholeNote / note_per_beat;
  int ticks_per_bar = std::max(1, beats_per_bar * ticks_per_beat);
  int cur_tick = std::max(0, start_tick);
  int remaining_ticks = duration_ticks;

  std::vector<NoteSymbolComponent> components;

  while (remaining_ticks > 0) {
    int bar_index = cur_tick / ticks_per_bar;
    int next_bar_tick = (bar_index + 1) * ticks_per_bar;
    int ticks_in_bar = std::min(remaining_ticks, next_bar_tick - cur_tick);

    int seg_remaining = ticks_in_bar;
    int seg_start_tick = cur_tick;

    while (seg_remaining > 0) {
      // Find largest matching symbol
      const SymbolDef* best_match = nullptr;
      for (const auto& sym : kSymbols) {
        if (sym.total_ticks <= seg_remaining) {
          best_match = &sym;
          break;
        }
      }

      if (!best_match) {
        // Fallback to smallest note symbol (TwoHundredAndFiftySixth)
        best_match = &kSymbols[sizeof(kSymbols) / sizeof(kSymbols[0]) - 1];
      }

      NoteSymbolComponent comp;
      comp.type = best_match->type;
      comp.duration_ticks = best_match->total_ticks;
      comp.is_dotted = best_match->is_dotted;
      comp.start_tick = seg_start_tick;
      comp.tick_offset = seg_start_tick - start_tick;
      comp.bar_index = bar_index;
      comp.ties_to_next = false;
      comp.tie_span_ticks = 0;
      comp.rest = rest;

      components.push_back(comp);

      int consumed = std::min(seg_remaining, best_match->total_ticks);
      if (consumed <= 0) consumed = 1;
      seg_remaining -= consumed;
      seg_start_tick += consumed;
    }

    cur_tick += ticks_in_bar;
    remaining_ticks -= ticks_in_bar;
  }

  // Link components with ties (only for notes, not rests)
  size_t count = components.size();
  for (size_t i = 0; i < count; ++i) {
    if (!rest && i + 1 < count) {
      components[i].ties_to_next = true;
      components[i].tie_span_ticks =
          components[i + 1].start_tick - components[i].start_tick;
    }
    on_symbol(components[i]);
  }
}

int GetTrackLastBarIndex(const std::vector<NoteSpan>& notes, int beats_per_bar,
                         int note_per_beat) {
  if (notes.empty()) return -1;
  int ticks_per_beat = kTicksPerWholeNote / note_per_beat;
  int ticks_per_bar = std::max(1, beats_per_bar * ticks_per_beat);
  int max_end_tick = 0;
  for (const auto& note : notes) {
    if (note.duration_ticks > 0) {
      max_end_tick =
          std::max(max_end_tick, note.start_tick + note.duration_ticks);
    }
  }
  if (max_end_tick <= 0) return -1;
  return (max_end_tick - 1) / ticks_per_bar;
}

int GetTrackLastBarEndTick(const std::vector<NoteSpan>& notes,
                           int beats_per_bar, int note_per_beat) {
  int last_bar_idx = GetTrackLastBarIndex(notes, beats_per_bar, note_per_beat);
  if (last_bar_idx < 0) return 0;
  int ticks_per_beat = kTicksPerWholeNote / note_per_beat;
  int ticks_per_bar = std::max(1, beats_per_bar * ticks_per_beat);
  return (last_bar_idx + 1) * ticks_per_bar;
}

std::vector<TimeInterval> CalculateStaffRests(
    const std::vector<NoteSpan>& notes, bool is_treble_staff,
    int track_last_bar_end_tick) {
  std::vector<TimeInterval> rests;
  if (track_last_bar_end_tick <= 0) return rests;

  std::vector<TimeInterval> occupied;
  for (const auto& note : notes) {
    if (note.duration_ticks <= 0) continue;
    bool is_treble_note = (note.key_index >= 39);
    if (is_treble_note == is_treble_staff) {
      occupied.push_back(
          {note.start_tick, note.start_tick + note.duration_ticks});
    }
  }

  std::sort(occupied.begin(), occupied.end(),
            [](const TimeInterval& a, const TimeInterval& b) {
              return a.start_tick < b.start_tick;
            });

  std::vector<TimeInterval> merged;
  for (const auto& interval : occupied) {
    if (merged.empty()) {
      merged.push_back(interval);
    } else {
      auto& back = merged.back();
      if (interval.start_tick <= back.end_tick) {
        back.end_tick = std::max(back.end_tick, interval.end_tick);
      } else {
        merged.push_back(interval);
      }
    }
  }

  int current_tick = 0;
  for (const auto& occ : merged) {
    if (occ.start_tick > current_tick) {
      int gap_end = std::min(occ.start_tick, track_last_bar_end_tick);
      if (gap_end > current_tick) {
        rests.push_back({current_tick, gap_end});
      }
    }
    current_tick = std::max(current_tick, occ.end_tick);
    if (current_tick >= track_last_bar_end_tick) break;
  }

  if (current_tick < track_last_bar_end_tick)
    rests.push_back({current_tick, track_last_bar_end_tick});

  return rests;
}

}  // namespace notation
