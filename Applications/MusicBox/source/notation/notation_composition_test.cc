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

#include <vector>

#include "testing.h"

namespace {

using namespace notation;

TEST(DecomposeNote_StandardNotes) {

  // Maxima (2048 ticks)
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(0, 2048, 32, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Maxima, (int)comps[0].type);
  EXPECT(2048, comps[0].duration_ticks);
  EXPECT(false, comps[0].is_dotted);
  EXPECT(false, comps[0].ties_to_next);

  // Longa (1024 ticks)
  comps.clear();
  DecomposeNote(0, 1024, 16, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Longa, (int)comps[0].type);
  EXPECT(1024, comps[0].duration_ticks);

  // Breve (512 ticks)
  comps.clear();
  DecomposeNote(0, 512, 8, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Breve, (int)comps[0].type);
  EXPECT(512, comps[0].duration_ticks);

  // Whole (256 ticks)
  comps.clear();
  DecomposeNote(0, 256, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Whole, (int)comps[0].type);
  EXPECT(256, comps[0].duration_ticks);

  // Half (128 ticks)
  comps.clear();
  DecomposeNote(0, 128, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Half, (int)comps[0].type);
  EXPECT(128, comps[0].duration_ticks);

  // Quarter (64 ticks)
  comps.clear();
  DecomposeNote(0, 64, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Quarter, (int)comps[0].type);
  EXPECT(64, comps[0].duration_ticks);

  // Eighth (32 ticks)
  comps.clear();
  DecomposeNote(0, 32, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Eighth, (int)comps[0].type);
  EXPECT(32, comps[0].duration_ticks);

  // Sixteenth (16 ticks)
  comps.clear();
  DecomposeNote(0, 16, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Sixteenth, (int)comps[0].type);
  EXPECT(16, comps[0].duration_ticks);

  // ThirtySecond (8 ticks)
  comps.clear();
  DecomposeNote(0, 8, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::ThirtySecond, (int)comps[0].type);
  EXPECT(8, comps[0].duration_ticks);

  // SixtyFourth (4 ticks)
  comps.clear();
  DecomposeNote(0, 4, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::SixtyFourth, (int)comps[0].type);
  EXPECT(4, comps[0].duration_ticks);

  // OneHundredTwentyEighth (2 ticks)
  comps.clear();
  DecomposeNote(0, 2, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::OneHundredTwentyEighth, (int)comps[0].type);
  EXPECT(2, comps[0].duration_ticks);

  // TwoHundredAndFiftySixth (1 tick)
  comps.clear();
  DecomposeNote(0, 1, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::TwoHundredAndFiftySixth, (int)comps[0].type);
  EXPECT(1, comps[0].duration_ticks);
}

TEST(DecomposeNote_DottedNotes) {
  // Dotted Half Note (192 ticks = 128 + 64)
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(0, 192, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Half, (int)comps[0].type);
  EXPECT(192, comps[0].duration_ticks);
  EXPECT(true, comps[0].is_dotted);

  // Dotted Quarter Note (96 ticks = 64 + 32)
  comps.clear();
  DecomposeNote(0, 96, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Quarter, (int)comps[0].type);
  EXPECT(true, comps[0].is_dotted);
}

TEST(DecomposeNote_TwoNoteTies) {
  // 0.875 beat = 224 ticks = 192 (Dotted Half) + 32 (8th note)
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(0, 224, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)2, comps.size());

  // Component 0: Dotted Half note (192 ticks)
  EXPECT((int)NoteSymbolType::Half, (int)comps[0].type);
  EXPECT(192, comps[0].duration_ticks);
  EXPECT(true, comps[0].is_dotted);
  EXPECT(true, comps[0].ties_to_next);
  EXPECT(192, comps[0].tie_span_ticks);

  // Component 1: 8th note (32 ticks)
  EXPECT((int)NoteSymbolType::Eighth, (int)comps[1].type);
  EXPECT(32, comps[1].duration_ticks);
  EXPECT(false, comps[1].is_dotted);
  EXPECT(false, comps[1].ties_to_next);
}

TEST(DecomposeNote_BarBoundarySplitting) {
  // Half note (128 ticks) starting at tick 192 in a 256-tick bar (beats_per_bar
  // = 4) Crosses bar line at tick 256 -> Bar 0 gets 64 ticks (Quarter), Bar 1
  // gets 64 ticks (Quarter)
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(192, 128, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)2, comps.size());

  // Component 0 in Bar 0
  EXPECT(0, comps[0].bar_index);
  EXPECT(192, comps[0].start_tick);
  EXPECT(64, comps[0].duration_ticks);
  EXPECT((int)NoteSymbolType::Quarter, (int)comps[0].type);
  EXPECT(true, comps[0].ties_to_next);

  // Component 1 in Bar 1
  EXPECT(1, comps[1].bar_index);
  EXPECT(256, comps[1].start_tick);
  EXPECT(64, comps[1].duration_ticks);
  EXPECT((int)NoteSymbolType::Quarter, (int)comps[1].type);
  EXPECT(false, comps[1].ties_to_next);
}

TEST(DecomposeNote_MultiBarCrossings) {
  // 600 ticks starting at tick 200 in 256-tick bars (beats_per_bar = 4)
  // Bar 0: tick 200 to 256 = 56 ticks (Dotted 8th [48] + 32nd [8])
  // Bar 1: tick 256 to 512 = 256 ticks (Whole [256])
  // Bar 2: tick 512 to 800 (rem 288 ticks) -> 256 ticks (Whole [256]) + Bar 3
  // rem 32 ticks (8th [32])
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(200, 600, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });

  EXPECT(true, comps.size() >= 4);
  EXPECT(0, comps[0].bar_index);
  EXPECT(true, comps[0].ties_to_next);
}

TEST(DecomposeNote_NotePerBeat) {
  // 1 beat note (64 composition ticks) with note_per_beat = 4 -> Quarter Note
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(0, 64, 4, 4,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Quarter, (int)comps[0].type);

  // 1 beat note (32 composition ticks) with note_per_beat = 8 -> Eighth Note
  comps.clear();
  DecomposeNote(0, 32, 4, 8,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Eighth, (int)comps[0].type);

  // 1 beat note (128 composition ticks) with note_per_beat = 2 -> Half Note
  comps.clear();
  DecomposeNote(0, 128, 4, 2,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Half, (int)comps[0].type);

  // 1 beat note (256 composition ticks) with note_per_beat = 1 -> Whole Note
  comps.clear();
  DecomposeNote(0, 256, 4, 1,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Whole, (int)comps[0].type);
}

TEST(DecomposeNote_SmallestNoteClamping) {
  // 1 composition tick with note_per_beat = 8 (which produces 0
  // eff_symbol_ticks) must clamp to TwoHundredAndFiftySixth symbol
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(0, 1, 4, 8,
                [&](const NoteSymbolComponent& c) { comps.push_back(c); });
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::TwoHundredAndFiftySixth, (int)comps[0].type);
}

TEST(SheetView_DiatonicStepMapping) {
  // C4 (Middle C) -> Key 39
  // Step for C4 = 21 (3 octaves * 7 steps)
  int step_c4 = (39 - 3) / 12 * 7 + 0;
  EXPECT(21, step_c4);

  // E4 -> Key 43 (Treble bottom line step 23)
  int step_e4 = (43 - 3) / 12 * 7 + 2;
  EXPECT(23, step_e4);
}

TEST(SheetView_BarPaddingAndMeasureCalc) {
  int beats_per_bar = 4;
  int ticks_per_bar = beats_per_bar * 64;  // 256 ticks per bar
  EXPECT(256, ticks_per_bar);

  // Bar index for tick 500 = 500 / 256 = 1 (Bar 2, 0-indexed index 1)
  int bar_idx_500 = 500 / ticks_per_bar;
  EXPECT(1, bar_idx_500);

  // Bar padding offset for 12.0f padding per bar
  float padding_offset = bar_idx_500 * 12.0f;
  EXPECT(12.0f, padding_offset);
}

TEST(SheetView_VerticalCenteringCalculation) {
  float staff_h = 212.0f;
  float view_h = 400.0f;

  int idx_0 = 0;
  float staff_center_0 = idx_0 * staff_h + staff_h / 2.0f;
  float scroll_y_0 = std::max(0.0f, staff_center_0 - view_h / 2.0f);
  EXPECT(0.0f, scroll_y_0);

  int idx_2 = 2;
  float staff_center_2 = idx_2 * staff_h + staff_h / 2.0f;
  float scroll_y_2 = std::max(0.0f, staff_center_2 - view_h / 2.0f);
  EXPECT(330.0f, scroll_y_2);
}

TEST(SheetView_A0_C8_Spacing) {
  float staff_h = 212.0f;
  float line_spacing = 8.0f;
  float step_spacing = line_spacing * 0.5f;

  // E4 is staff step 30
  // Note A0 is Key 0 -> diatonic step -2
  int step_a0 = -2;
  float ny_a0_inst0 =
      (0 * staff_h + staff_h * 0.5f - 20.0f) - (step_a0 - 30) * step_spacing;

  // Note C8 is Key 87 -> diatonic step 49
  int step_c8 = 49;
  float ny_c8_inst1 =
      (1 * staff_h + staff_h * 0.5f - 20.0f) - (step_c8 - 30) * step_spacing;

  float gap = ny_c8_inst1 - ny_a0_inst0;
  EXPECT(8.0f, gap);
}

TEST(DecomposeNote_Rests) {
  // Whole rest (256 ticks)
  std::vector<NoteSymbolComponent> comps;
  DecomposeNote(
      0, 256, 4, 4, [&](const NoteSymbolComponent& c) { comps.push_back(c); },
      /*rest=*/true);
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Whole, (int)comps[0].type);
  EXPECT(256, comps[0].duration_ticks);
  EXPECT(true, comps[0].rest);
  EXPECT(false, comps[0].ties_to_next);

  // Dotted Half rest (192 ticks)
  comps.clear();
  DecomposeNote(
      0, 192, 4, 4, [&](const NoteSymbolComponent& c) { comps.push_back(c); },
      /*rest=*/true);
  ASSERT((size_t)1, comps.size());
  EXPECT((int)NoteSymbolType::Half, (int)comps[0].type);
  EXPECT(true, comps[0].is_dotted);
  EXPECT(true, comps[0].rest);
  EXPECT(false, comps[0].ties_to_next);

  // Multi-bar rest across bar boundary (256 ticks starting at 128 in 256-tick
  // bar) Crosses bar line -> 128 ticks (Half rest) in Bar 0, 128 ticks (Half
  // rest) in Bar 1
  comps.clear();
  DecomposeNote(
      128, 256, 4, 4, [&](const NoteSymbolComponent& c) { comps.push_back(c); },
      /*rest=*/true);
  ASSERT((size_t)2, comps.size());
  EXPECT(0, comps[0].bar_index);
  EXPECT(true, comps[0].rest);
  EXPECT(false, comps[0].ties_to_next);
  EXPECT(1, comps[1].bar_index);
  EXPECT(true, comps[1].rest);
  EXPECT(false, comps[1].ties_to_next);
}

TEST(Track_LastBarCalculation) {
  // Empty track
  std::vector<NoteSpan> empty_notes;
  EXPECT(-1, GetTrackLastBarIndex(empty_notes, 4, 4));
  EXPECT(0, GetTrackLastBarEndTick(empty_notes, 4, 4));

  // Single note in Bar 0 (0-64 ticks)
  std::vector<NoteSpan> notes = {{0, 64, 45}};
  EXPECT(0, GetTrackLastBarIndex(notes, 4, 4));
  EXPECT(256, GetTrackLastBarEndTick(notes, 4, 4));

  // Notes reaching into Bar 2 (ticks 500-550)
  notes.push_back({500, 50, 45});
  // 550 ticks total -> (550 - 1) / 256 = Bar 2 (0-indexed index 2) -> Last bar
  // end tick = 3 * 256 = 768
  EXPECT(2, GetTrackLastBarIndex(notes, 4, 4));
  EXPECT(768, GetTrackLastBarEndTick(notes, 4, 4));
}

TEST(Staff_CalculateStaffRests_SingleStaff) {
  // Note from tick 0 to 64, and note from tick 192 to 256 (both on Treble staff
  // key 50 >= 39). Track last bar end tick = 256.
  std::vector<NoteSpan> notes = {{0, 64, 50}, {192, 64, 50}};
  auto rests = CalculateStaffRests(notes, /*is_treble_staff=*/true, 256);
  ASSERT((size_t)1, rests.size());
  EXPECT(64, rests[0].start_tick);
  EXPECT(192, rests[0].end_tick);

  // Bass staff has no notes, up to 256 -> gets rest from 0 to 256
  auto bass_rests = CalculateStaffRests(notes, /*is_treble_staff=*/false, 256);
  ASSERT((size_t)1, bass_rests.size());
  EXPECT(0, bass_rests[0].start_tick);
  EXPECT(256, bass_rests[0].end_tick);
}

TEST(Staff_CalculateStaffRests_DualStaffGaps) {
  // Bass staff note (key 30 < 39) in Bar 0 (0-256 ticks).
  // Treble staff note (key 50 >= 39) in Bar 1 (256-512 ticks).
  // Track last bar end tick = 512.
  std::vector<NoteSpan> notes = {{0, 256, 30}, {256, 256, 50}};

  // Treble staff has gap in Bar 0 (0-256)
  auto treble_rests = CalculateStaffRests(notes, /*is_treble_staff=*/true, 512);
  ASSERT((size_t)1, treble_rests.size());
  EXPECT(0, treble_rests[0].start_tick);
  EXPECT(256, treble_rests[0].end_tick);

  // Bass staff has gap in Bar 1 (256-512)
  auto bass_rests = CalculateStaffRests(notes, /*is_treble_staff=*/false, 512);
  ASSERT((size_t)1, bass_rests.size());
  EXPECT(256, bass_rests[0].start_tick);
  EXPECT(512, bass_rests[0].end_tick);
}

TEST(SheetView_TimeSignatureVerticalAndHorizontalLayout) {
  // Line 4 and Line 2 baselines for 5-line staff centered at y_staff_center
  float y_staff_center = 100.0f;
  float line_spacing = 8.0f;

  float num_y = y_staff_center - line_spacing + 4.5f;  // Line 4 baseline
  float den_y = y_staff_center + line_spacing + 4.5f;  // Line 2 baseline

  EXPECT_APPROX(96.5f, num_y, 0.01f);
  EXPECT_APPROX(112.5f, den_y, 0.01f);
}

}  // namespace
