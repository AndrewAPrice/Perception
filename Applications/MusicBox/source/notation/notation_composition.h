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

#include <functional>
#include <vector>

namespace notation {

enum class NoteSymbolType {
  Maxima,                  // 2048 ticks
  Longa,                   // 1024 ticks
  Breve,                   // 512 ticks
  Whole,                   // 256 ticks
  Half,                    // 128 ticks
  Quarter,                 // 64 ticks
  Eighth,                  // 32 ticks
  Sixteenth,               // 16 ticks
  ThirtySecond,            // 8 ticks
  SixtyFourth,             // 4 ticks
  OneHundredTwentyEighth,  // 2 ticks
  TwoHundredAndFiftySixth  // 1 tick
};

struct NoteSymbolComponent {
  NoteSymbolType type;
  int duration_ticks;  // Base symbol duration in 256th ticks (e.g. 128 = 1/2
                       // note)
  bool is_dotted;      // True if symbol has a dot (+50% duration)
  int start_tick;      // Absolute start tick of this component
  int tick_offset;     // Offset from note start_tick
  int bar_index;       // Bar index (0-indexed) where component resides
  bool ties_to_next;   // True if tied to the next component
  int tie_span_ticks;  // Duration/length of tie line connecting to next symbol
  bool rest = false;   // True if symbol is a rest
};

// Zero-allocation decomposition callback function type
using OnSymbolCallback =
    std::function<void(const NoteSymbolComponent& component)>;

// Decomposes a note or rest with start_tick and duration_ticks into musical
// symbols, invoking on_symbol callback for each component. Splits across bar
// boundaries. note_per_beat specifies which note type receives one beat (e.g. 4
// = quarter note, 8 = eighth note, 2 = half note, 1 = whole note).
// note_per_beat must be a power of 2 and >= 1.
void DecomposeNote(int start_tick, int duration_ticks, int beats_per_bar,
                   int note_per_beat, const OnSymbolCallback& on_symbol,
                   bool rest = false);

struct NoteSpan {
  int start_tick = 0;
  int duration_ticks = 0;
  int key_index = 0;
};

struct TimeInterval {
  int start_tick = 0;
  int end_tick = 0;
};

// Calculates 0-indexed last bar containing any notes for a track. Returns -1 if
// track has no notes.
int GetTrackLastBarIndex(const std::vector<NoteSpan>& notes, int beats_per_bar,
                         int note_per_beat);

// Calculates the end tick of the last bar containing any notes for a track.
// Returns 0 if track has no notes.
int GetTrackLastBarEndTick(const std::vector<NoteSpan>& notes,
                           int beats_per_bar, int note_per_beat);

// Calculates rest gap intervals for a specific staff (Treble vs Bass) up to
// track_last_bar_end_tick.
std::vector<TimeInterval> CalculateStaffRests(
    const std::vector<NoteSpan>& notes, bool is_treble_staff,
    int track_last_bar_end_tick);

}  // namespace notation
