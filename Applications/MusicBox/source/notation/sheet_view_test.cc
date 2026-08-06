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

#include "notation/sheet_view.h"

#include "testing.h"
#include "track_manager.h"

namespace {

using namespace notation;

TEST(SheetView_DiatonicStepMapping) {

  // C4 (Middle C) -> Key 39
  // Step for C4 = 28
  int step_c4 = (39 - 3) / 12 * 7 + 0;  // 3 * 7 = 21 + 7 = 28
  EXPECT(28, step_c4);

  // E4 -> Key 43 (Treble bottom line step 30)
  int step_e4 = (43 - 3) / 12 * 7 + 2;  // 28 + 2 = 30
  EXPECT(30, step_e4);
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

TEST(SheetView_PenAndEraserCursor) {
  TrackManager tm;
  SheetView sv(tm);
  auto node = sv.GetNode();
  EXPECT(true, node != nullptr);

  // Set node size so hover logic functions
  node->SetSize(800.0f, 600.0f);

  // Hover over blank notation space -> Pen cursor
  node->MouseHover(perception::ui::Point{200.0f, 100.0f});
  EXPECT(true, node->GetCursor() == perception::window::Cursor::Pen);

  // Control key held (erase mode) -> Eraser cursor
  sv.SetControlDown(true);
  node->MouseHover(perception::ui::Point{200.0f, 100.0f});
  EXPECT(true, node->GetCursor() == perception::window::Cursor::Eraser);

  // Control key released -> Pen cursor
  sv.SetControlDown(false);
  node->MouseHover(perception::ui::Point{200.0f, 100.0f});
  EXPECT(true, node->GetCursor() == perception::window::Cursor::Pen);
}

TEST(SheetView_DefaultZoomLevel) {
  TrackManager tm;
  SheetView sv(tm);
  // Default zoom level should be 0.4096f (1.0 / 1.25^4)
  EXPECT_APPROX(0.4096f, sv.GetZoomLevel(), 0.001f);

  // Zooming in 4 times should reach 1.0f
  sv.ZoomIn();
  sv.ZoomIn();
  sv.ZoomIn();
  sv.ZoomIn();
  EXPECT_APPROX(1.0f, sv.GetZoomLevel(), 0.001f);

  // Resetting zoom should restore zoom level to default 0.4096f
  sv.ResetZoom();
  EXPECT_APPROX(0.4096f, sv.GetZoomLevel(), 0.001f);
}

TEST(SheetView_TimeSignatureVerticalAndHorizontalLayout) {
  TrackManager tm;
  SheetView sv(tm);

  // Default time signature is 4/4
  EXPECT(4, sv.GetBeatsPerBar());
  EXPECT(4, sv.GetNotePerBeat());

  // Test updating time signature
  sv.SetBeatsPerBar(3);
  sv.SetNotePerBeat(4);
  EXPECT(3, sv.GetBeatsPerBar());
  EXPECT(4, sv.GetNotePerBeat());

  // Check line spacing vertical math for Line 4 and Line 2
  float y_staff_center = 100.0f;
  float line_spacing = 8.0f;

  float num_y = y_staff_center - line_spacing + 4.5f;  // Line 4 baseline
  float den_y = y_staff_center + line_spacing + 4.5f;  // Line 2 baseline

  EXPECT_APPROX(96.5f, num_y, 0.01f);
  EXPECT_APPROX(112.5f, den_y, 0.01f);
}

TEST(SheetView_MeasureBarLineAndNoteClearance) {
  float cursor_x = 80.0f;
  float pixels_per_tick = 1.0f;
  int beats_per_bar = 4;
  int cur_tick = 0;

  // Measure 0 bar line position vs note 0 position
  float bx_0 = GetBarLineX(0, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);
  float nx_0 = GetTickX(0, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);

  // Note at tick 0 should be at cursor_x (80.0f)
  EXPECT_APPROX(80.0f, nx_0, 0.001f);

  // Bar line 0 should be kBarLeftMarginPx (18.0f) to the left of note 0
  EXPECT_APPROX(62.0f, bx_0, 0.001f);
  EXPECT_APPROX(kBarLeftMarginPx, nx_0 - bx_0, 0.001f);

  // Accidental (#) is drawn at nx_0 - 11.0f -> 69.0f
  float accidental_x = nx_0 - 11.0f;
  EXPECT_APPROX(69.0f, accidental_x, 0.001f);

  // Clearance between accidental and bar line should be 7.0f px (never overlapping)
  EXPECT_APPROX(7.0f, accidental_x - bx_0, 0.001f);
}

TEST(SheetView_MeasureEndingBarLineClearance) {
  float cursor_x = 80.0f;
  float pixels_per_tick = 0.5f;  // Low zoom level
  int beats_per_bar = 4;
  int ticks_per_bar = beats_per_bar * 64;  // 256
  int cur_tick = 0;

  // Last tick in measure 0 (tick 255)
  float nx_255 =
      GetTickX(ticks_per_bar - 1, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);

  // Measure 1 bar line
  float bx_1 = GetBarLineX(1, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);

  // Notehead right edge (rx = 4.5f)
  float notehead_right_edge = nx_255 + 4.5f;

  // Gap between notehead right edge and next bar line must be >= kBarRightMarginPx (10.0f)
  EXPECT(true, bx_1 - notehead_right_edge >= kBarRightMarginPx);
}

TEST(SheetView_InverseLayoutMappingRoundTrip) {
  float cursor_x = 80.0f;
  float pixels_per_tick = 1.2f;
  int beats_per_bar = 4;
  int cur_tick = 100;

  for (int tick : {0, 64, 128, 255, 256, 300, 512}) {
    float x = GetTickX(tick, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);
    int recovered_tick =
        GetTickAtX(x, cur_tick, cursor_x, pixels_per_tick, beats_per_bar);
    EXPECT(tick, recovered_tick);
  }
}

}  // namespace
