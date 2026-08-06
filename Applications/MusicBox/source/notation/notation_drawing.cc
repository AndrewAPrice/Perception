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

#include "notation/notation_drawing.h"

#include <algorithm>
#include <cmath>

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "constants.h"
#include "notation/sheet_view.h"
#include "perception/ui/font.h"
#include "track_manager.h"

namespace notation {

namespace {

// Diatonic step threshold above which note stems point downward.
constexpr int kStemDirectionStepThreshold = 34;

// Diatonic step range bounds for Alto clef ledger lines.
constexpr int kAltoClefMinStep = 24;
constexpr int kAltoClefBottomLedgerStart = 22;
constexpr int kAltoClefMaxStep = 32;
constexpr int kAltoClefTopLedgerStart = 34;

// Diatonic step range bounds for Tenor clef ledger lines.
constexpr int kTenorClefMinStep = 22;
constexpr int kTenorClefBottomLedgerStart = 20;
constexpr int kTenorClefMaxStep = 30;
constexpr int kTenorClefTopLedgerStart = 32;

// Diatonic step range bounds for Grand Staff ledger lines.
constexpr int kGrandStaffMinStep = 18;
constexpr int kGrandStaffBottomLedgerStart = 16;
constexpr int kGrandStaffMaxStep = 38;
constexpr int kGrandStaffTopLedgerStart = 40;

// Y-offset of E4 relative to the staff center line.
constexpr float kStaffE4YOffset = 20.0f;

// Minimum horizontal width of a hovered note block.
constexpr float kMinHoverBlockWidth = 8.0f;

// Vertical radius ratio of a hovered note block relative to line spacing.
constexpr float kHoverBlockVerticalRadiusRatio = 0.45f;

// Corner radius of a hovered note block rounded rectangle.
constexpr float kHoverBlockCornerRadius = 4.0f;

// Color of the hovered note block highlight border (Amber).
constexpr uint32_t kHoverBorderColor = 0xFFD97706;

// Stroke width of the hovered note block border.
constexpr float kHoverBorderStrokeWidth = 2.0f;

// Horizontal margin used for offscreen symbol culling.
constexpr float kSymbolCullingMargin = 20.0f;

// Half width of whole and half rest rectangles.
constexpr float kRestHalfWidth = 5.0f;

// Stroke width for quarter rest glyph paths.
constexpr float kQuarterRestStrokeWidth = 2.0f;

// Offsets for the quarter rest squiggly path relative to note position.
constexpr float kQuarterRestPoint1XOffset = 2.0f;
constexpr float kQuarterRestPoint1YOffset = -6.0f;
constexpr float kQuarterRestPoint2XOffset = -3.0f;
constexpr float kQuarterRestPoint2YOffset = -2.0f;
constexpr float kQuarterRestPoint3XOffset = 3.0f;
constexpr float kQuarterRestPoint3YOffset = 2.0f;
constexpr float kQuarterRestPoint4XOffset = -2.0f;
constexpr float kQuarterRestPoint4YOffset = 6.0f;

// Eighth rest dot offsets and radius.
constexpr float kEighthRestDotXOffset = -2.0f;
constexpr float kEighthRestDotYOffset = -3.0f;
constexpr float kEighthRestDotRadius = 2.0f;

// Eighth rest stem offsets.
constexpr float kEighthRestStemTopYOffset = -3.0f;
constexpr float kEighthRestStemBottomXOffset = -3.5f;
constexpr float kEighthRestStemBottomYOffset = 5.0f;

// Rest stem stroke width.
constexpr float kRestStemStrokeWidth = 1.5f;

// Sixteenth and shorter rest dot offsets and radius.
constexpr float kSixteenthRestDotXOffset = -2.0f;
constexpr float kSixteenthRestTopDotYOffset = -5.0f;
constexpr float kSixteenthRestBottomDotYOffset = -1.0f;
constexpr float kSixteenthRestDotRadius = 1.8f;

// Sixteenth and shorter rest stem offsets.
constexpr float kSixteenthRestStemTopYOffset = -5.0f;
constexpr float kSixteenthRestStemBottomXOffset = -3.5f;
constexpr float kSixteenthRestStemBottomYOffset = 6.0f;

// Dotted rest augmentation dot offsets and radius.
constexpr float kDottedRestDotXOffset = 8.0f;
constexpr float kDottedRestDotYOffset = -2.0f;
constexpr float kDottedRestDotRadius = 1.8f;

// Ledger line stroke width and half-width.
constexpr float kLedgerLineStrokeWidth = 1.5f;
constexpr float kLedgerLineHalfWidth = 7.0f;

// Accidental font size, offsets, and color.
constexpr float kAccidentalFontSize = 11.0f;
constexpr float kAccidentalXOffset = -11.0f;
constexpr float kAccidentalYOffset = 4.0f;

// Notehead horizontal and vertical radii.
constexpr float kNoteheadRadiusX = 4.5f;
constexpr float kNoteheadRadiusY = 3.2f;

// Hollow notehead outline stroke width.
constexpr float kHollowNoteheadStrokeWidth = 1.8f;

// Augmentation dot offsets and radius for noteheads.
constexpr float kNoteDotXOffset = 3.5f;
constexpr float kNoteDotYOffset = -1.0f;
constexpr float kNoteDotRadius = 1.8f;

// Stem stroke width and length multiplier relative to line spacing.
constexpr float kStemStrokeWidth = 1.5f;
constexpr float kStemLengthLineSpacingMultiplier = 3.2f;

// Flag offsets relative to stem tip.
constexpr float kFlagXOffset = 5.0f;
constexpr float kFlagYOffset = 4.0f;

// Tie arc stroke width, start/end offsets, and control point Y offset.
constexpr float kTieStrokeWidth = 1.8f;
constexpr float kTieStartXOffset = 2.0f;
constexpr float kTieStartYOffset = -5.0f;
constexpr float kTieArcYOffset = -14.0f;
constexpr float kTieEndXOffset = 2.0f;
constexpr float kTieEndYOffset = -5.0f;

bool IsAccidentalKey(int key_index) {
  return TrackManager::KeyIndexToNoteName(key_index).find('#') !=
         std::string::npos;
}

}  // namespace

void DrawHoveredNoteBlock(SkCanvas& canvas, int key_index, float start_x,
                          float dur_px, float staff_center_y,
                          float line_spacing, float header_w, float view_w,
                          const NoteRenderStyle& style) {
  if (start_x + dur_px < header_w || start_x > view_w) return;

  int diatonic_step = GetDiatonicStep(key_index);
  float y_e4 = staff_center_y - kStaffE4YOffset;
  float ny = y_e4 - (diatonic_step - kDiatonicStepE4) * (line_spacing * 0.5f);

  float block_l = std::max(header_w, start_x);
  float block_r =
      std::min(view_w, start_x + std::max(kMinHoverBlockWidth, dur_px));
  float block_t = ny - (line_spacing * kHoverBlockVerticalRadiusRatio);
  float block_b = ny + (line_spacing * kHoverBlockVerticalRadiusRatio);

  if (block_r > block_l) {
    SkRRect rrect;
    rrect.setRectXY(SkRect::MakeLTRB(block_l, block_t, block_r, block_b),
                    kHoverBlockCornerRadius, kHoverBlockCornerRadius);

    SkPaint block_paint;
    block_paint.setColor(style.note_color);
    block_paint.setAntiAlias(true);

    SkPaint border_paint;
    border_paint.setColor(kHoverBorderColor);
    border_paint.setStyle(SkPaint::kStroke_Style);
    border_paint.setStrokeWidth(kHoverBorderStrokeWidth);
    border_paint.setAntiAlias(true);

    canvas.drawRRect(rrect, block_paint);
    canvas.drawRRect(rrect, border_paint);
  }
}

void DrawSymbolComponentOnStaff(
    SkCanvas& canvas, const NoteSymbolComponent& component, int key_index,
    float cursor_x, float pixels_per_tick, int cur_time_ms, float bpm,
    float staff_center_y, float line_spacing, float header_w, float view_w,
    int beats_per_bar, const NoteRenderStyle& style) {
  int cur_tick = TrackManager::MsToTicks(cur_time_ms, bpm);
  float nx = GetTickX(component.start_tick, cur_tick, cursor_x, pixels_per_tick,
                      beats_per_bar);

  if (nx < header_w - kSymbolCullingMargin ||
      nx > view_w + kSymbolCullingMargin)
    return;

  if (component.rest) {
    // Rest rendering path centered at staff_center_y
    float ny = staff_center_y;

    SkPaint rest_paint;
    rest_paint.setColor(style.note_color);
    rest_paint.setAntiAlias(true);

    if (component.type == NoteSymbolType::Whole ||
        component.type == NoteSymbolType::Breve ||
        component.type == NoteSymbolType::Maxima) {
      // Whole rest: hangs down from Line 4 (ny - line_spacing)
      float top_y = ny - line_spacing;
      canvas.drawRect(
          SkRect::MakeLTRB(nx - kRestHalfWidth, top_y, nx + kRestHalfWidth,
                           top_y + 0.5f * line_spacing),
          rest_paint);
    } else if (component.type == NoteSymbolType::Half) {
      // Half rest: sits on top of Line 3 (ny)
      float bot_y = ny;
      canvas.drawRect(
          SkRect::MakeLTRB(nx - kRestHalfWidth, bot_y - 0.5f * line_spacing,
                           nx + kRestHalfWidth, bot_y),
          rest_paint);
    } else if (component.type == NoteSymbolType::Quarter) {
      // Quarter rest: squiggly path
      SkPaint stroke_paint = rest_paint;
      stroke_paint.setStyle(SkPaint::kStroke_Style);
      stroke_paint.setStrokeWidth(kQuarterRestStrokeWidth);
      SkPathBuilder builder;
      builder.moveTo(nx + kQuarterRestPoint1XOffset,
                     ny + kQuarterRestPoint1YOffset);
      builder.lineTo(nx + kQuarterRestPoint2XOffset,
                     ny + kQuarterRestPoint2YOffset);
      builder.lineTo(nx + kQuarterRestPoint3XOffset,
                     ny + kQuarterRestPoint3YOffset);
      builder.lineTo(nx + kQuarterRestPoint4XOffset,
                     ny + kQuarterRestPoint4YOffset);
      canvas.drawPath(builder.detach(), stroke_paint);
    } else if (component.type == NoteSymbolType::Eighth) {
      // Eighth rest: hook and slanted stem
      canvas.drawCircle(nx + kEighthRestDotXOffset, ny + kEighthRestDotYOffset,
                        kEighthRestDotRadius, rest_paint);
      SkPaint stem_paint = rest_paint;
      stem_paint.setStrokeWidth(kRestStemStrokeWidth);
      canvas.drawLine(nx, ny + kEighthRestStemTopYOffset,
                      nx + kEighthRestStemBottomXOffset,
                      ny + kEighthRestStemBottomYOffset, stem_paint);
    } else {
      // 16th and shorter rests
      canvas.drawCircle(nx + kSixteenthRestDotXOffset,
                        ny + kSixteenthRestTopDotYOffset,
                        kSixteenthRestDotRadius, rest_paint);
      canvas.drawCircle(nx + kSixteenthRestDotXOffset,
                        ny + kSixteenthRestBottomDotYOffset,
                        kSixteenthRestDotRadius, rest_paint);
      SkPaint stem_paint = rest_paint;
      stem_paint.setStrokeWidth(kRestStemStrokeWidth);
      canvas.drawLine(nx, ny + kSixteenthRestStemTopYOffset,
                      nx + kSixteenthRestStemBottomXOffset,
                      ny + kSixteenthRestStemBottomYOffset, stem_paint);
    }

    if (component.is_dotted)
      canvas.drawCircle(nx + kDottedRestDotXOffset, ny + kDottedRestDotYOffset,
                        kDottedRestDotRadius, rest_paint);
    return;
  }

  int diatonic_step = GetDiatonicStep(key_index);
  // E4 is staff step 30 (bottom line of treble staff)
  float y_line5 = staff_center_y - kStaffE4YOffset;
  float ny =
      y_line5 - (diatonic_step - kDiatonicStepE4) * (line_spacing * 0.5f);

  SkPaint note_paint;
  note_paint.setColor(style.note_color);
  note_paint.setAntiAlias(true);

  SkPaint line_paint;
  line_paint.setColor(kDefaultLineColor);
  line_paint.setStrokeWidth(kLedgerLineStrokeWidth);
  line_paint.setAntiAlias(true);

  // Ledger Lines
  auto draw_ledger = [&](int s) {
    float ly = y_line5 - (s - kDiatonicStepE4) * (line_spacing * 0.5f);
    canvas.drawLine(nx - kLedgerLineHalfWidth, ly, nx + kLedgerLineHalfWidth,
                    ly, line_paint);
  };

  if (style.clef == Clef::Alto) {
    if (diatonic_step < kAltoClefMinStep) {
      for (int s = kAltoClefBottomLedgerStart; s >= diatonic_step; s -= 2)
        draw_ledger(s);
    } else if (diatonic_step > kAltoClefMaxStep) {
      for (int s = kAltoClefTopLedgerStart; s <= diatonic_step; s += 2)
        draw_ledger(s);
    }
  } else if (style.clef == Clef::Tenor) {
    if (diatonic_step < kTenorClefMinStep) {
      for (int s = kTenorClefBottomLedgerStart; s >= diatonic_step; s -= 2)
        draw_ledger(s);
    } else if (diatonic_step > kTenorClefMaxStep) {
      for (int s = kTenorClefTopLedgerStart; s <= diatonic_step; s += 2)
        draw_ledger(s);
    }
  } else {
    // TrebleAndBass (Grand Staff)
    if (diatonic_step < kGrandStaffMinStep) {
      for (int s = kGrandStaffBottomLedgerStart; s >= diatonic_step; s -= 2)
        draw_ledger(s);
    } else if (diatonic_step == kDiatonicStepMiddleC) {
      draw_ledger(kDiatonicStepMiddleC);
    } else if (diatonic_step > kGrandStaffMaxStep) {
      for (int s = kGrandStaffTopLedgerStart; s <= diatonic_step; s += 2)
        draw_ledger(s);
    }
  }

  // Accidental Sharp (#)
  if (IsAccidentalKey(key_index)) {
    SkPaint acc_paint;
    acc_paint.setColor(kDefaultLineColor);
    acc_paint.setAntiAlias(true);
    static SkFont* acc_font =
        ::perception::ui::GetUiFont("", kAccidentalFontSize, /*bold=*/true);
    canvas.drawString("#", nx + kAccidentalXOffset, ny + kAccidentalYOffset,
                      *acc_font, acc_paint);
  }

  // Notehead & Stem
  float rx = kNoteheadRadiusX;
  float ry = kNoteheadRadiusY;

  bool is_hollow = (component.type == NoteSymbolType::Whole ||
                    component.type == NoteSymbolType::Breve ||
                    component.type == NoteSymbolType::Maxima ||
                    component.type == NoteSymbolType::Half);

  if (is_hollow) {
    SkPaint hollow_paint = note_paint;
    hollow_paint.setStyle(SkPaint::kStroke_Style);
    hollow_paint.setStrokeWidth(kHollowNoteheadStrokeWidth);
    canvas.drawOval(SkRect::MakeLTRB(nx - rx, ny - ry, nx + rx, ny + ry),
                    hollow_paint);
  } else {
    canvas.drawOval(SkRect::MakeLTRB(nx - rx, ny - ry, nx + rx, ny + ry),
                    note_paint);
  }

  // Dot
  if (component.is_dotted) {
    SkPaint dot_paint = note_paint;
    canvas.drawCircle(nx + rx + kNoteDotXOffset, ny + kNoteDotYOffset,
                      kNoteDotRadius, dot_paint);
  }

  // Stems & Flags (for Half and shorter)
  if (component.type != NoteSymbolType::Whole &&
      component.type != NoteSymbolType::Breve &&
      component.type != NoteSymbolType::Maxima) {
    float stem_dir =
        (diatonic_step >= kStemDirectionStepThreshold) ? 1.0f : -1.0f;
    float stem_x = nx + (stem_dir < 0 ? rx : -rx);
    float stem_top_y =
        ny + stem_dir * (line_spacing * kStemLengthLineSpacingMultiplier);

    SkPaint stem_paint = note_paint;
    stem_paint.setStrokeWidth(kStemStrokeWidth);
    canvas.drawLine(stem_x, ny, stem_x, stem_top_y, stem_paint);

    // Flag for 8th and shorter notes
    if (component.type == NoteSymbolType::Eighth ||
        component.type == NoteSymbolType::Sixteenth ||
        component.type == NoteSymbolType::ThirtySecond ||
        component.type == NoteSymbolType::SixtyFourth ||
        component.type == NoteSymbolType::OneHundredTwentyEighth ||
        component.type == NoteSymbolType::TwoHundredAndFiftySixth) {
      canvas.drawLine(stem_x, stem_top_y, stem_x + kFlagXOffset,
                      stem_top_y - stem_dir * kFlagYOffset, stem_paint);
    }
  }

  // Connecting Tie Curve across symbol / bar boundary
  if (component.ties_to_next && component.tie_span_ticks > 0) {
    int next_start_tick = component.start_tick + component.tie_span_ticks;
    float tie_end_x = GetTickX(next_start_tick, cur_tick, cursor_x,
                               pixels_per_tick, beats_per_bar);

    if (tie_end_x > nx) {
      SkPaint tie_paint;
      tie_paint.setColor(style.note_color);
      tie_paint.setStyle(SkPaint::kStroke_Style);
      tie_paint.setStrokeWidth(kTieStrokeWidth);
      tie_paint.setAntiAlias(true);

      SkPathBuilder builder;
      builder.moveTo(nx + kTieStartXOffset, ny + kTieStartYOffset);
      float mid_x = (nx + tie_end_x) / 2.0f;
      float arc_y = ny + kTieArcYOffset;
      builder.quadTo(mid_x, arc_y, tie_end_x - kTieEndXOffset,
                     ny + kTieEndYOffset);

      canvas.drawPath(builder.detach(), tie_paint);
    }
  }
}

}  // namespace notation
