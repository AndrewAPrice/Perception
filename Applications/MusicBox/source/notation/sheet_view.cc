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

#include <algorithm>
#include <cmath>

#include "undo_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "constants.h"
#include "notation/notation_composition.h"
#include "notation/notation_drawing.h"
#include "perception/time.h"
#include "perception/ui/components/focusable.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "synth_engine.h"

using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Size;
using ::perception::ui::components::Focusable;
namespace window = ::perception::window;

namespace notation {

namespace {

// Total vertical height allocated per staff system in pixels.
constexpr float kStaffHeight = 212.0f;

// Default sheet view scroll speed multiplier.
constexpr float kDefaultSheetSpeed = 0.14f;

// Fallback view height when node size is not yet measured.
constexpr float kFallbackViewHeight = 400.0f;

// Minimum and default ticks per bar / beat calculations.
constexpr int kMinTicksPerBar = 64;
constexpr float kTicksPerBeat = 64.0f;

// Default staffing line spacing in pixels.
constexpr float kLineSpacing = 8.0f;

// Y-offset of E4 relative to staff center line.
constexpr float kStaffE4YOffset = 20.0f;

// Horizontal layout offsets for staff margin, line left, clef, time signature,
// and cursor.
constexpr float kStaffLeftMargin = 20.0f;
constexpr float kLineLeftOffset = 6.0f;
constexpr float kClefLeftOffset = 12.0f;
constexpr float kTimeSigLeftOffset = 36.0f;
constexpr float kCursorLeftOffset = 60.0f;
constexpr float kDefaultCursorX = 80.0f;
constexpr float kClefHoverWidth = 55.0f;

// Bar left and right padding.
constexpr float kBarPaddingPx = 12.0f;

// Staff stroke widths and clef scale factor.
constexpr float kStaffLineStrokeWidth = 1.5f;
constexpr float kClefScale = 0.8f;

// Treble clef drawing constants.
constexpr float kTrebleClefStrokeWidth = 2.2f;
constexpr float kTrebleClefDotRadius = 2.5f;

// Bass clef drawing constants.
constexpr float kBassClefStrokeWidth = 2.5f;
constexpr float kBassClefHeadDotRadius = 3.2f;
constexpr float kBassClefDotRadius = 2.0f;

// C clef drawing constants.
constexpr float kCClefLineStrokeWidth = 1.5f;
constexpr float kCClefThickStrokeWidth = 3.0f;
constexpr float kCClefArcStrokeWidth = 2.0f;
constexpr float kCClefCenterDotRadius = 2.0f;

// Grand staff brace drawing constants.
constexpr float kBraceStrokeWidth = 2.0f;
constexpr float kBraceWidth = 7.0f;

// Time signature drawing constants.
constexpr float kTimeSigFontSize = 13.0f;
constexpr float kTimeSigTextYOffset = 4.5f;

// Clef vertical staff offset constants.
constexpr float kAltoStaffCenterOffset = 12.0f;
constexpr float kAltoBottomYOffset = 4.0f;
constexpr float kAltoTopYOffset = 28.0f;

constexpr float kTenorStaffCenterOffset = 4.0f;
constexpr float kTenorBottomYOffset = 12.0f;
constexpr float kTenorTopYOffset = 20.0f;

constexpr float kTrebleLine5YOffset = 20.0f;
constexpr float kTrebleTopYOffset = 52.0f;
constexpr float kBassLine5YOffset = 40.0f;

// Measure bar line and measure number rendering constants.
constexpr int kExtraDurationBufferMs = 5000;
constexpr float kBarLineStrokeWidth = 1.5f;
constexpr float kBarNumberFontSize = 9.0f;
constexpr float kBarLineMinXOffset = 15.0f;
constexpr float kBarLineTopPadding = 10.0f;
constexpr float kBarLineBottomPadding = 4.0f;
constexpr float kBarNumberXOffset = 2.0f;
constexpr float kBarNumberYPadding = 8.0f;

// Default key indices for rest decomposition on different staves.
constexpr int kAltoTenorRestKeyIndex = 48;
constexpr int kTrebleRestKeyIndex = 60;
constexpr int kBassRestKeyIndex = 30;

// Track title floating header layout constants.
constexpr float kTrebleTitleYOffset = 68.0f;
constexpr float kAltoTitleYOffset = 44.0f;
constexpr float kTenorTitleYOffset = 36.0f;
constexpr float kTitleFontSize = 13.0f;
constexpr float kTitleBgYOffset = 12.0f;
constexpr float kTitleBgHeight = 18.0f;
constexpr float kTitleBgWidthPadding = 8.0f;
constexpr float kTitleTextXOffset = 4.0f;
constexpr float kTitleTextYOffset = 2.0f;

// Playback cursor line styling.
constexpr float kCursorLineStrokeWidth = 2.0f;

// Time display overlay constants.
constexpr float kTimeDisplayFontSize = 12.0f;
constexpr float kTimeDisplayMarginRight = 12.0f;
constexpr float kTimeDisplayY = 20.0f;

// Hit testing thresholds.
constexpr float kMinHoverNoteWidth = 12.0f;
constexpr float kHitTestVerticalRadius = 6.0f;
constexpr float kResizeEdgeThreshold = 4.0f;

// Colors (ARGB) and text styles.
constexpr float kEmptyStateFontSize = 14.0f;
constexpr uint32_t kSheetBackgroundColor = 0xFFFFFFFF;
constexpr uint32_t kEmptyStateTextColor = 0xFF666666;
constexpr uint32_t kBarLineColor = 0xFF888888;
constexpr uint32_t kBarNumberColor = 0xFF555555;
constexpr uint32_t kCursorLineColor = 0xFFE53935;
constexpr uint32_t kTimeDisplayColor = 0xFF45475A;

}  // namespace

float CalculateAbsoluteTickPosition(int tick, float pixels_per_tick,
                                    int beats_per_bar) {
  if (tick < 0) tick = 0;
  int ticks_per_bar =
      std::max(kMinTicksPerBar, beats_per_bar * kMinTicksPerBar);
  float w_notes = static_cast<float>(ticks_per_bar) * pixels_per_tick;
  float w_measure = kBarLeftMarginPx + w_notes + kBarRightMarginPx;

  int b = tick / ticks_per_bar;
  int dt = tick % ticks_per_bar;
  return static_cast<float>(b) * w_measure + kBarLeftMarginPx +
         static_cast<float>(dt) * pixels_per_tick;
}

float CalculateAbsoluteBarLinePosition(int bar_index, float pixels_per_tick,
                                       int beats_per_bar) {
  if (bar_index < 0) bar_index = 0;
  int ticks_per_bar =
      std::max(kMinTicksPerBar, beats_per_bar * kMinTicksPerBar);
  float w_notes = static_cast<float>(ticks_per_bar) * pixels_per_tick;
  float w_measure = kBarLeftMarginPx + w_notes + kBarRightMarginPx;
  return static_cast<float>(bar_index) * w_measure;
}

float GetTickX(int tick, int cur_tick, float cursor_x, float pixels_per_tick,
               int beats_per_bar) {
  float p_tick =
      CalculateAbsoluteTickPosition(tick, pixels_per_tick, beats_per_bar);
  float p_cur =
      CalculateAbsoluteTickPosition(cur_tick, pixels_per_tick, beats_per_bar);
  return cursor_x + p_tick - p_cur;
}

float GetBarLineX(int bar_index, int cur_tick, float cursor_x,
                  float pixels_per_tick, int beats_per_bar) {
  float p_bar = CalculateAbsoluteBarLinePosition(bar_index, pixels_per_tick,
                                                 beats_per_bar);
  float p_cur =
      CalculateAbsoluteTickPosition(cur_tick, pixels_per_tick, beats_per_bar);
  return cursor_x + p_bar - p_cur;
}

int GetTickAtX(float x, int cur_tick, float cursor_x, float pixels_per_tick,
               int beats_per_bar) {
  float p_cur =
      CalculateAbsoluteTickPosition(cur_tick, pixels_per_tick, beats_per_bar);
  float p_target = p_cur + x - cursor_x;
  int ticks_per_bar =
      std::max(kMinTicksPerBar, beats_per_bar * kMinTicksPerBar);
  float w_notes = static_cast<float>(ticks_per_bar) * pixels_per_tick;
  float w_measure = kBarLeftMarginPx + w_notes + kBarRightMarginPx;

  if (p_target <= 0.0f) return 0;

  int b = static_cast<int>(std::floor(p_target / w_measure));
  if (b < 0) b = 0;

  float rem = p_target - (static_cast<float>(b) * w_measure + kBarLeftMarginPx);
  if (rem < 0.0f) rem = 0.0f;

  float eff_ppt = pixels_per_tick > 0.0f ? pixels_per_tick : 1.0f;
  int dt =
      std::min(ticks_per_bar - 1, static_cast<int>(std::floor(rem / eff_ppt)));
  return b * ticks_per_bar + dt;
}

namespace {

void DrawTrebleClef(SkCanvas& canvas, float x, float y_center, float scale,
                    uint32 color) {
  SkPaint paint;
  paint.setColor(color);
  paint.setAntiAlias(true);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(kTrebleClefStrokeWidth * scale);

  SkPathBuilder builder;
  // Main vertical stem and top loop
  builder.moveTo(x + 7.0f * scale, y_center + 16.0f * scale);
  builder.cubicTo(x + 7.0f * scale, y_center + 18.0f * scale, x + 3.0f * scale,
                  y_center + 18.0f * scale, x + 2.0f * scale,
                  y_center + 15.0f * scale);                   // bottom hook
  builder.lineTo(x + 7.0f * scale, y_center - 22.0f * scale);  // stem up
  builder.cubicTo(x + 7.0f * scale, y_center - 30.0f * scale, x + 14.0f * scale,
                  y_center - 28.0f * scale, x + 10.0f * scale,
                  y_center - 16.0f * scale);  // top loop
  builder.cubicTo(x + 6.0f * scale, y_center - 4.0f * scale, x - 1.0f * scale,
                  y_center + 2.0f * scale, x + 5.0f * scale,
                  y_center + 8.0f * scale);  // lower outer curve
  builder.cubicTo(x + 13.0f * scale, y_center + 12.0f * scale,
                  x + 16.0f * scale, y_center + 3.0f * scale, x + 12.0f * scale,
                  y_center - 4.0f * scale);  // right spiral curve
  builder.cubicTo(x + 8.0f * scale, y_center - 9.0f * scale, x + 4.0f * scale,
                  y_center - 3.0f * scale, x + 7.0f * scale,
                  y_center + 1.0f * scale);  // inner spiral

  canvas.drawPath(builder.detach(), paint);

  // Bottom dot
  SkPaint dot_paint;
  dot_paint.setColor(color);
  dot_paint.setAntiAlias(true);
  canvas.drawCircle(x + 2.0f * scale, y_center + 14.0f * scale,
                    kTrebleClefDotRadius * scale, dot_paint);
}

void DrawBassClef(SkCanvas& canvas, float x, float y_center, float scale,
                  uint32 color) {
  SkPaint paint;
  paint.setColor(color);
  paint.setAntiAlias(true);

  // Main F-clef curve
  SkPaint stroke_paint = paint;
  stroke_paint.setStyle(SkPaint::kStroke_Style);
  stroke_paint.setStrokeWidth(kBassClefStrokeWidth * scale);

  SkPathBuilder builder;
  builder.moveTo(x + 4.0f * scale, y_center);
  builder.cubicTo(x + 4.0f * scale, y_center - 9.0f * scale, x + 14.0f * scale,
                  y_center - 9.0f * scale, x + 14.0f * scale,
                  y_center - 1.0f * scale);
  builder.cubicTo(x + 14.0f * scale, y_center + 6.0f * scale, x + 8.0f * scale,
                  y_center + 14.0f * scale, x + 4.0f * scale,
                  y_center + 16.0f * scale);

  canvas.drawPath(builder.detach(), stroke_paint);

  // Solid head dot on line 4
  canvas.drawCircle(x + 4.0f * scale, y_center, kBassClefHeadDotRadius * scale,
                    paint);

  // Two dots surrounding line 4 (F3)
  canvas.drawCircle(x + 17.0f * scale, y_center - 4.0f * scale,
                    kBassClefDotRadius * scale, paint);
  canvas.drawCircle(x + 17.0f * scale, y_center + 4.0f * scale,
                    kBassClefDotRadius * scale, paint);
}

void DrawCClef(SkCanvas& canvas, float x, float y_center, float scale,
               uint32 color) {
  SkPaint paint;
  paint.setColor(color);
  paint.setAntiAlias(true);

  // Left vertical thin line
  SkPaint line_paint = paint;
  line_paint.setStyle(SkPaint::kStroke_Style);
  line_paint.setStrokeWidth(kCClefLineStrokeWidth * scale);

  canvas.drawLine(x + 2.0f * scale, y_center - 14.0f * scale, x + 2.0f * scale,
                  y_center + 14.0f * scale, line_paint);

  // Left vertical thick line
  SkPaint thick_paint = paint;
  thick_paint.setStyle(SkPaint::kStroke_Style);
  thick_paint.setStrokeWidth(kCClefThickStrokeWidth * scale);

  canvas.drawLine(x + 5.0f * scale, y_center - 14.0f * scale, x + 5.0f * scale,
                  y_center + 14.0f * scale, thick_paint);

  // Upper & Lower arcs pointing to y_center
  SkPaint arc_paint = paint;
  arc_paint.setStyle(SkPaint::kStroke_Style);
  arc_paint.setStrokeWidth(kCClefArcStrokeWidth * scale);

  SkPathBuilder builder;
  // Upper C-arc
  builder.moveTo(x + 5.0f * scale, y_center - 14.0f * scale);
  builder.cubicTo(x + 14.0f * scale, y_center - 12.0f * scale,
                  x + 14.0f * scale, y_center - 2.0f * scale, x + 7.0f * scale,
                  y_center);

  // Lower C-arc
  builder.moveTo(x + 5.0f * scale, y_center + 14.0f * scale);
  builder.cubicTo(x + 14.0f * scale, y_center + 12.0f * scale,
                  x + 14.0f * scale, y_center + 2.0f * scale, x + 7.0f * scale,
                  y_center);

  canvas.drawPath(builder.detach(), arc_paint);

  // Small center pointer dot at Middle C
  canvas.drawCircle(x + 7.0f * scale, y_center, kCClefCenterDotRadius * scale,
                    paint);
}

void DrawGrandStaffBrace(SkCanvas& canvas, float x, float top_y, float bot_y,
                         uint32 color) {
  SkPaint paint;
  paint.setColor(color);
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(kBraceStrokeWidth);
  paint.setAntiAlias(true);

  float mid_y = (top_y + bot_y) * 0.5f;
  float w = kBraceWidth;

  SkPathBuilder builder;
  builder.moveTo(x + w, top_y);
  builder.quadTo(x, top_y, x, (top_y + mid_y) * 0.5f);
  builder.quadTo(x, mid_y, x - w, mid_y);

  builder.moveTo(x - w, mid_y);
  builder.quadTo(x, mid_y, x, (mid_y + bot_y) * 0.5f);
  builder.quadTo(x, bot_y, x + w, bot_y);

  canvas.drawPath(builder.detach(), paint);
}

void DrawTimeSignature(SkCanvas& canvas, float x, float y_staff_center,
                       float line_spacing, int beats_per_bar, int note_per_beat,
                       uint32 color) {
  static SkFont* font =
      ::perception::ui::GetUiFont("", kTimeSigFontSize, /*bold=*/true);

  static int cached_beats_per_bar = -1;
  static std::string cached_num_str;
  static float cached_num_w = 0.0f;

  static int cached_note_per_beat = -1;
  static std::string cached_den_str;
  static float cached_den_w = 0.0f;

  if (cached_beats_per_bar != beats_per_bar) {
    cached_beats_per_bar = beats_per_bar;
    cached_num_str = std::to_string(beats_per_bar);
    cached_num_w = font->measureText(
        cached_num_str.c_str(), cached_num_str.length(), SkTextEncoding::kUTF8);
  }

  if (cached_note_per_beat != note_per_beat) {
    cached_note_per_beat = note_per_beat;
    cached_den_str = std::to_string(note_per_beat);
    cached_den_w = font->measureText(
        cached_den_str.c_str(), cached_den_str.length(), SkTextEncoding::kUTF8);
  }

  float max_w = std::max(cached_num_w, cached_den_w);
  float num_x = x + (max_w - cached_num_w) * 0.5f;
  float den_x = x + (max_w - cached_den_w) * 0.5f;

  SkPaint text_paint;
  text_paint.setColor(color);
  text_paint.setAntiAlias(true);

  // Center numerator on Line 4 (y_staff_center - line_spacing)
  // Center denominator on Line 2 (y_staff_center + line_spacing)
  float num_y = y_staff_center - line_spacing + kTimeSigTextYOffset;
  float den_y = y_staff_center + line_spacing + kTimeSigTextYOffset;

  canvas.drawString(cached_num_str.c_str(), num_x, num_y, *font, text_paint);
  canvas.drawString(cached_den_str.c_str(), den_x, den_y, *font, text_paint);
}

}  // namespace

SheetView::SheetView(TrackManager& track_manager)
    : NotationView(track_manager, kDefaultSheetSpeed) {
  BuildNode();
}

void SheetView::CenterOnTrack(int track_id) {
  const auto& tracks = track_manager_.GetTracks();
  std::vector<const Track*> visible_tracks;
  for (const auto& track : tracks) {
    if (!track.muted && !track.hidden) visible_tracks.push_back(&track);
  }
  int idx = -1;
  for (size_t i = 0; i < visible_tracks.size(); ++i) {
    if (visible_tracks[i]->id == track_id) {
      idx = static_cast<int>(i);
      break;
    }
  }
  if (idx < 0) return;

  float view_h = node_ ? node_->GetSize().height : 0.0f;
  if (view_h <= 0.0f) view_h = kFallbackViewHeight;

  float staff_center = idx * kStaffHeight + kStaffHeight / 2.0f;
  scroll_y_offset_px_ = std::max(0.0f, staff_center - view_h / 2.0f);
  Invalidate();
}

void SheetView::OnTrackSelected(int track_id, bool auto_scroll) {
  if (auto_scroll) {
    CenterOnTrack(track_id);
  } else {
    Invalidate();
  }
}

void SheetView::BuildNode() {
  node_ = Node::Empty(
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetWidthPercent(100.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMargin(YGEdgeBottom, 4.0f);
      },
      [this](Node& node) {
        node.OnDraw([this](const DrawContext& context) {
          if (!context.skia_canvas) return;
          context.skia_canvas->save();
          context.skia_canvas->translate(context.area.origin.x,
                                         context.area.origin.y);
          context.skia_canvas->clipRect(
              SkRect::MakeWH(context.area.Width(), context.area.Height()));
          DrawSheetView(*context.skia_canvas, context.area.Width(),
                        context.area.Height());

          context.skia_canvas->restore();
        });

        auto focusable = node.GetOrAdd<Focusable>();

        // Mouse Hover & Interaction
        node.OnMouseHover([this, focusable](const Point& pt) {
          if (!focusable->HasFocus()) focusable->Focus();
          Size s = node_->GetSize();
          float w = s.width;
          float h = s.height;

          if (space_down_ && !input_focused_ && !track_manager_.IsPlaying()) {
            if (node_->GetCursor() != perception::window::Cursor::Grab)
              node_->SetCursor(perception::window::Cursor::Grab);
            last_mouse_pos_ = pt;
            if (drag_state_ == DragState::Panning) {
              float eff_speed = speed_ * zoom_level_;
              if (eff_speed <= 0.0f) eff_speed = kDefaultSheetSpeed;
              float dx = pt.x - pan_start_mouse_pos_.x;
              float dy = pt.y - pan_start_mouse_pos_.y;
              int delta_ms = static_cast<int>(dx / eff_speed);
              int song_dur = track_manager_.GetSongDurationMs();
              int new_time =
                  std::clamp(pan_start_time_ms_ - delta_ms, 0, song_dur);
              track_manager_.Seek(new_time);
              scroll_y_offset_px_ = std::max(0.0f, pan_start_scroll_y_ - dy);
              Invalidate();
            }
            return;
          }

          last_mouse_pos_ = pt;

          if (track_manager_.IsPlaying()) {
            if (node_->GetCursor() != perception::window::Cursor::Pointer)
              node_->SetCursor(perception::window::Cursor::Pointer);
            int prev_hover_track = hover_track_id_;
            int prev_hover_note = hover_note_index_;
            hover_track_id_ = 0;
            hover_note_index_ = -1;
            if (prev_hover_track != 0 || prev_hover_note != -1) Invalidate();
            return;
          }

          // Check Clef Area Hover (pointer cursor when hovering over clef)
          float staff_left_x = kStaffLeftMargin;
          if (pt.x >= staff_left_x && pt.x <= staff_left_x + kClefHoverWidth) {
            std::vector<const Track*> visible_tracks;
            for (const auto& trk : track_manager_.GetTracks()) {
              if (!trk.muted && !trk.hidden) visible_tracks.push_back(&trk);
            }
            int staff_idx = static_cast<int>(
                std::floor((pt.y + scroll_y_offset_px_) / kStaffHeight));
            if (staff_idx >= 0 &&
                staff_idx < static_cast<int>(visible_tracks.size())) {
              if (node_->GetCursor() != perception::window::Cursor::Pointer)
                node_->SetCursor(perception::window::Cursor::Pointer);
              return;
            }
          }

          // Active Drag / Resize / Draw
          if (drag_state_ != DragState::None) {
            Track* drag_track = track_manager_.GetTrack(drag_track_id_);
            if (!drag_track || drag_note_index_ < 0 ||
                drag_note_index_ >=
                    static_cast<int>(drag_track->notes.size())) {
              drag_state_ = DragState::None;
              drag_track_id_ = 0;
              drag_note_index_ = -1;
              return;
            }
            auto& note = drag_track->notes[drag_note_index_];

            float staff_h = kStaffHeight;
            float staff_center_y = staff_h * 0.5f - scroll_y_offset_px_;
            std::vector<const Track*> visible_tracks;
            for (const auto& trk : track_manager_.GetTracks()) {
              if (!trk.muted && !trk.hidden) visible_tracks.push_back(&trk);
            }
            for (size_t idx = 0; idx < visible_tracks.size(); ++idx) {
              if (visible_tracks[idx]->id == drag_track_id_) {
                staff_center_y =
                    idx * staff_h + staff_h * 0.5f - scroll_y_offset_px_;
                break;
              }
            }

            if (drag_state_ == DragState::MovingNote) {
              if (node_->GetCursor() != perception::window::Cursor::Drag)
                node_->SetCursor(perception::window::Cursor::Drag);
              int target_time_ms = GetTimeMsAtX(pt.x, w);
              int target_start_tick =
                  SnapTick(track_manager_.MsToTicks(target_time_ms));

              int target_key =
                  GetKeyIndexAtY(pt.y, staff_center_y, kLineSpacing);

              if (track_manager_.CanPlaceNote(
                      drag_track_id_, target_key, target_start_tick,
                      note.duration_ticks, drag_note_index_)) {
                note.key_index = target_key;
                note.start_tick = target_start_tick;
                track_manager_.SyncTrackNotesMs(*drag_track);

                if (target_key != last_preview_key_) {
                  TriggerPreviewSound(target_key, drag_track->instrument,
                                      drag_track->volume,
                                      track_manager_.GetBpm());
                }
              }
            } else if (drag_state_ == DragState::DrawingNote) {
              int target_time_ms = GetTimeMsAtX(pt.x, w);
              int cur_tick = SnapTick(track_manager_.MsToTicks(target_time_ms));
              int start_tick = std::min(draw_start_tick_, cur_tick);
              int end_tick = std::max(draw_start_tick_, cur_tick) + snap_ticks_;
              int dur_ticks = std::max(snap_ticks_, end_tick - start_tick);

              int target_key =
                  GetKeyIndexAtY(pt.y, staff_center_y, kLineSpacing);

              if (track_manager_.CanPlaceNote(drag_track_id_, target_key,
                                              start_tick, dur_ticks,
                                              drag_note_index_)) {
                note.key_index = target_key;
                note.start_tick = start_tick;
                note.duration_ticks = dur_ticks;
                track_manager_.SyncTrackNotesMs(*drag_track);

                if (target_key != last_preview_key_) {
                  TriggerPreviewSound(target_key, drag_track->instrument,
                                      drag_track->volume,
                                      track_manager_.GetBpm());
                }
              }
            }
            Invalidate();
            return;
          }

          // Hover Cursors and update hover state (when not dragging)
          int prev_hover_track = hover_track_id_;
          int prev_hover_note = hover_note_index_;

          if (control_down_) {
            if (node_->GetCursor() != perception::window::Cursor::Eraser)
              node_->SetCursor(perception::window::Cursor::Eraser);
            auto hit = HitTestNotes(pt.x, pt.y, w, h);
            if (hit.note_index >= 0) {
              hover_track_id_ = hit.track_id;
              hover_note_index_ = hit.note_index;
            } else {
              hover_track_id_ = 0;
              hover_note_index_ = -1;
            }
          } else {
            auto hit = HitTestNotes(pt.x, pt.y, w, h);
            if (hit.note_index >= 0) {
              hover_track_id_ = hit.track_id;
              hover_note_index_ = hit.note_index;
              if (hit.is_left_edge || hit.is_right_edge) {
                if (node_->GetCursor() !=
                    perception::window::Cursor::ResizeHorizontal) {
                  node_->SetCursor(
                      perception::window::Cursor::ResizeHorizontal);
                }
              } else {
                if (node_->GetCursor() != perception::window::Cursor::Drag)
                  node_->SetCursor(perception::window::Cursor::Drag);
              }
            } else {
              hover_track_id_ = 0;
              hover_note_index_ = -1;
              if (node_->GetCursor() != perception::window::Cursor::Pen)
                node_->SetCursor(perception::window::Cursor::Pen);
            }
          }

          if (hover_track_id_ != prev_hover_track ||
              hover_note_index_ != prev_hover_note) {
            Invalidate();
          }
        });

        // Mouse Down Callback
        node.OnMouseButtonDown([this](const Point& pt,
                                      perception::window::MouseButton button) {
          if (track_manager_.IsPlaying()) return;

          if (space_down_ && !input_focused_) {
            drag_state_ = DragState::Panning;
            pan_start_mouse_pos_ = pt;
            pan_start_time_ms_ = track_manager_.GetCurrentTimeMs();
            pan_start_scroll_y_ = scroll_y_offset_px_;
            Invalidate();
            return;
          }

          Size s = node_->GetSize();
          float w = s.width;
          float h = s.height;

          // Check Clef Area Click -> Cycle Clef
          float staff_left_x = kStaffLeftMargin;
          if (button == perception::window::MouseButton::Left &&
              pt.x >= staff_left_x && pt.x <= staff_left_x + kClefHoverWidth) {
            std::vector<Track*> visible_tracks;
            for (auto& trk : track_manager_.GetTracks()) {
              if (!trk.muted && !trk.hidden) visible_tracks.push_back(&trk);
            }
            int staff_idx = static_cast<int>(
                std::floor((pt.y + scroll_y_offset_px_) / kStaffHeight));
            if (staff_idx >= 0 &&
                staff_idx < static_cast<int>(visible_tracks.size())) {
              Track* trk = visible_tracks[staff_idx];
              if (trk->clef == Clef::TrebleAndBass) {
                trk->clef = Clef::Alto;
              } else if (trk->clef == Clef::Alto) {
                trk->clef = Clef::Tenor;
              } else {
                trk->clef = Clef::TrebleAndBass;
              }
              Invalidate();
              return;
            }
          }

          // Hit Test Notes
          auto hit = HitTestNotes(pt.x, pt.y, w, h);
          if (hit.note_index >= 0) {
            Track* trk = track_manager_.GetTrack(hit.track_id);
            if (trk && hit.note_index < static_cast<int>(trk->notes.size())) {
              NotifyTrackSelected(hit.track_id, /*auto_scroll=*/false);

              if (control_down_) {
                NoteEvent deleted_note = trk->notes[hit.note_index];
                int deleted_trk_id = hit.track_id;
                trk->notes.erase(trk->notes.begin() + hit.note_index);
                if (undo_manager_) {
                  undo_manager_->PushAction(
                      std::make_unique<DeleteNoteUndoAction>(deleted_trk_id,
                                                             deleted_note));
                }
                hover_track_id_ = 0;
                hover_note_index_ = -1;
                drag_track_id_ = 0;
                drag_note_index_ = -1;
                Invalidate();
                return;
              }

              drag_track_id_ = hit.track_id;
              drag_note_index_ = hit.note_index;
              auto& note = trk->notes[hit.note_index];
              initial_note_start_tick_ = note.start_tick;
              initial_note_duration_ticks_ = note.duration_ticks;
              initial_note_key_index_ = note.key_index;
              initial_drag_note_ = note;

              drag_state_ = DragState::MovingNote;
              TriggerPreviewSound(note.key_index, trk->instrument, trk->volume,
                                  track_manager_.GetBpm());
              Invalidate();
              return;
            }
          }

          // Blank space click -> Place Note
          if (control_down_) return;

          std::vector<const Track*> visible_tracks;
          for (const auto& trk : track_manager_.GetTracks()) {
            if (!trk.muted && !trk.hidden) visible_tracks.push_back(&trk);
          }
          if (visible_tracks.empty()) return;

          float staff_h = kStaffHeight;
          int staff_idx = static_cast<int>(
              std::floor((pt.y + scroll_y_offset_px_) / staff_h));
          if (staff_idx < 0 ||
              staff_idx >= static_cast<int>(visible_tracks.size()))
            return;

          Track* active_track = const_cast<Track*>(visible_tracks[staff_idx]);
          int target_track_id = active_track->id;

          NotifyTrackSelected(target_track_id, /*auto_scroll=*/false);

          int click_time_ms = GetTimeMsAtX(pt.x, w);
          int start_tick = SnapTick(track_manager_.MsToTicks(click_time_ms));

          float staff_center_y =
              staff_idx * staff_h + staff_h * 0.5f - scroll_y_offset_px_;
          int key_idx = GetKeyIndexAtY(pt.y, staff_center_y, kLineSpacing);

          if (track_manager_.CanPlaceNote(active_track->id, key_idx, start_tick,
                                          snap_ticks_)) {
            NoteEvent new_note;
            new_note.key_index = key_idx;
            new_note.start_tick = start_tick;
            new_note.duration_ticks = snap_ticks_;
            new_note.start_time_ms = track_manager_.TicksToMs(start_tick);
            new_note.duration_ms = track_manager_.TicksToMs(snap_ticks_);
            new_note.velocity = kDefaultNoteVelocity;

            active_track->notes.push_back(new_note);
            drag_track_id_ = active_track->id;
            drag_note_index_ = active_track->notes.size() - 1;
            draw_start_tick_ = start_tick;
            drag_state_ = DragState::DrawingNote;

            TriggerPreviewSound(key_idx, active_track->instrument,
                                active_track->volume, track_manager_.GetBpm());
            Invalidate();
          }
        });

        // Mouse Up Callback
        node.OnMouseButtonUp(
            [this](const Point& pt, perception::window::MouseButton button) {
              StopPreviewSound();
              if (drag_state_ == DragState::Panning) {
                drag_state_ = DragState::None;
                Invalidate();
                return;
              }

              if (drag_track_id_ != 0 && drag_note_index_ >= 0) {
                Track* trk = track_manager_.GetTrack(drag_track_id_);
                if (trk && drag_note_index_ < static_cast<int>(trk->notes.size())) {
                  const auto& current_note = trk->notes[drag_note_index_];
                  if (drag_state_ == DragState::DrawingNote) {
                    if (undo_manager_) {
                      undo_manager_->PushAction(
                          std::make_unique<DrawNoteUndoAction>(drag_track_id_,
                                                               current_note));
                    }
                  } else if (drag_state_ == DragState::MovingNote ||
                             drag_state_ == DragState::ResizingNoteTop ||
                             drag_state_ == DragState::ResizingNoteBottom) {
                    if (current_note.key_index != initial_drag_note_.key_index ||
                        current_note.start_tick != initial_drag_note_.start_tick ||
                        current_note.duration_ticks !=
                            initial_drag_note_.duration_ticks) {
                      if (undo_manager_) {
                        undo_manager_->PushAction(
                            std::make_unique<ChangeNoteUndoAction>(
                                drag_track_id_, initial_drag_note_,
                                current_note));
                      }
                    }
                  }
                }
              }

              drag_state_ = DragState::None;
              drag_track_id_ = 0;
              drag_note_index_ = -1;

              Size s = node_->GetSize();
              float w = s.width;
              float h = s.height;
              auto hit = HitTestNotes(pt.x, pt.y, w, h);
              if (hit.note_index >= 0) {
                hover_track_id_ = hit.track_id;
                hover_note_index_ = hit.note_index;
              } else {
                hover_track_id_ = 0;
                hover_note_index_ = -1;
              }
              Invalidate();
            });

        // Mouse Leave Callback
        node.OnMouseLeave([this]() {
          StopPreviewSound();
          drag_state_ = DragState::None;
          drag_track_id_ = 0;
          drag_note_index_ = -1;
          hover_track_id_ = 0;
          hover_note_index_ = -1;
          node_->SetCursor(perception::window::Cursor::Pointer);
          Invalidate();
        });
      });
}

int SheetView::GetKeyIndexAtY(float y, float staff_center_y,
                              float line_spacing) {
  // E4 is staff step 30
  float y_line5 = staff_center_y - kStaffE4YOffset;
  float step_diff = (y_line5 - y) / (line_spacing * 0.5f);
  int diatonic_step = kDiatonicStepE4 + static_cast<int>(std::round(step_diff));

  // Map diatonic step to key index
  int octave = diatonic_step / kDiatonicStepsPerOctave;
  int step_in_octave = diatonic_step % kDiatonicStepsPerOctave;
  if (step_in_octave < 0) {
    step_in_octave += kDiatonicStepsPerOctave;
    octave -= 1;
  }
  static const int kStepToKeyOffset[7] = {0, 2, 4, 5,
                                          7, 9, 11};  // C, D, E, F, G, A, B
  int key = octave * kSemitonesPerOctave + kStepToKeyOffset[step_in_octave] +
            kKeyOffsetC;
  return std::clamp(key, 0, kMaxKeyIndex);
}

int SheetView::GetTimeMsAtX(float x, float w) {
  float cursor_x = kDefaultCursorX;
  float eff_speed = speed_ * zoom_level_;
  float bpm = track_manager_.GetBpm();
  float pixels_per_tick =
      eff_speed * (kMsPerMinute / (bpm > 0.0f ? bpm * kTicksPerBeat
                                              : kDefaultBpm * kTicksPerBeat));
  int cur_time = track_manager_.GetCurrentTimeMs();
  int cur_tick = TrackManager::MsToTicks(cur_time, bpm);

  int target_tick =
      GetTickAtX(x, cur_tick, cursor_x, pixels_per_tick, beats_per_bar_);
  return TrackManager::TicksToMs(target_tick, bpm);
}

SheetView::NoteHitResult SheetView::HitTestNotes(float x, float y, float w,
                                                 float h) {
  NoteHitResult result;
  float cursor_x = kDefaultCursorX;
  float eff_speed = speed_ * zoom_level_;
  float bpm = track_manager_.GetBpm();
  float pixels_per_tick =
      eff_speed * (kMsPerMinute / (bpm > 0.0f ? bpm * kTicksPerBeat
                                              : kDefaultBpm * kTicksPerBeat));
  int cur_time = track_manager_.GetCurrentTimeMs();
  int cur_tick = TrackManager::MsToTicks(cur_time, bpm);
  float staff_h = kStaffHeight;
  float line_spacing = kLineSpacing;

  std::vector<const Track*> visible_tracks;
  for (const auto& track : track_manager_.GetTracks()) {
    if (!track.muted && !track.hidden) {
      visible_tracks.push_back(&track);
    }
  }

  for (size_t t_idx = 0; t_idx < visible_tracks.size(); ++t_idx) {
    const auto& track = *visible_tracks[t_idx];

    float y_top = t_idx * staff_h - scroll_y_offset_px_;
    float staff_center_y = y_top + staff_h * 0.5f;
    float y_line5 = staff_center_y - kStaffE4YOffset;

    for (size_t n_idx = 0; n_idx < track.notes.size(); ++n_idx) {
      const auto& note = track.notes[n_idx];
      float nx = GetTickX(note.start_tick, cur_tick, cursor_x, pixels_per_tick,
                          beats_per_bar_);
      float end_x = GetTickX(note.start_tick + note.duration_ticks, cur_tick,
                             cursor_x, pixels_per_tick, beats_per_bar_);
      float dur_px = end_x - nx;

      int diatonic_step = GetDiatonicStep(note.key_index);
      float ny =
          y_line5 - (diatonic_step - kDiatonicStepE4) * (line_spacing * 0.5f);

      float rect_l = nx;
      float rect_r = nx + std::max(kMinHoverNoteWidth, dur_px);
      float rect_t = ny - kHitTestVerticalRadius;
      float rect_b = ny + kHitTestVerticalRadius;

      if (x >= rect_l && x <= rect_r && y >= rect_t && y <= rect_b) {
        result.track_id = track.id;
        result.note_index = static_cast<int>(n_idx);
        if (x - rect_l <= kResizeEdgeThreshold) result.is_left_edge = true;
        if (rect_r - x <= kResizeEdgeThreshold) result.is_right_edge = true;
        return result;
      }
    }
  }

  return result;
}

void SheetView::DrawSheetView(SkCanvas& canvas, float w, float h) {
  // White sheet parchment background
  SkPaint bg_paint;
  bg_paint.setColor(kSheetBackgroundColor);
  canvas.drawRect(SkRect::MakeXYWH(0, 0, w, h), bg_paint);

  // Filter unmuted and visible tracks
  std::vector<const Track*> visible_tracks;
  for (const auto& track : track_manager_.GetTracks()) {
    if (!track.muted && !track.hidden) {
      visible_tracks.push_back(&track);
    }
  }

  if (visible_tracks.empty()) {
    SkPaint text_paint;
    text_paint.setColor(kEmptyStateTextColor);
    text_paint.setAntiAlias(true);
    SkFont* font = ::perception::ui::GetUiFont("", kEmptyStateFontSize);
    canvas.drawString("No active unmuted tracks to render staff.",
                      kStaffLeftMargin, h / 2.0f, *font, text_paint);
    return;
  }

  int cur_time = track_manager_.GetCurrentTimeMs();
  float staff_left_x = kStaffLeftMargin;
  float line_left_x = staff_left_x + kLineLeftOffset;
  float clef_x = staff_left_x + kClefLeftOffset;
  float time_sig_x = staff_left_x + kTimeSigLeftOffset;
  float cursor_x = staff_left_x + kCursorLeftOffset;
  float eff_speed = speed_ * zoom_level_;
  float pixels_per_tick =
      eff_speed * (kMsPerMinute / (track_manager_.GetBpm() * kTicksPerBeat));
  float bar_padding_px = kBarPaddingPx;
  float staff_h = kStaffHeight;
  float line_spacing = kLineSpacing;

  for (size_t i = 0; i < visible_tracks.size(); ++i) {
    const auto& track = *visible_tracks[i];
    float y_top = i * staff_h - scroll_y_offset_px_;
    if (y_top + staff_h < 0 || y_top > h) continue;

    float staff_center_y = y_top + staff_h * 0.5f;

    SkPaint line_paint;
    line_paint.setColor(kDefaultLineColor);
    line_paint.setStrokeWidth(kStaffLineStrokeWidth);
    line_paint.setAntiAlias(true);

    // Staves, Clefs, Brace, and Time Signature
    if (track.clef == Clef::Alto) {
      float alto_y_bottom = staff_center_y + kAltoBottomYOffset;
      float alto_y_top = staff_center_y - kAltoTopYOffset;
      for (int l = 0; l < 5; ++l) {
        float ly = alto_y_bottom - l * line_spacing;
        canvas.drawLine(line_left_x, ly, w, ly, line_paint);
      }
      canvas.drawLine(line_left_x, alto_y_top, line_left_x, alto_y_bottom,
                      line_paint);
      DrawCClef(canvas, clef_x, staff_center_y - kAltoStaffCenterOffset,
                kClefScale, kDefaultLineColor);
      DrawTimeSignature(canvas, time_sig_x,
                        staff_center_y - kAltoStaffCenterOffset, line_spacing,
                        beats_per_bar_, note_per_beat_, kDefaultLineColor);
    } else if (track.clef == Clef::Tenor) {
      float tenor_y_bottom = staff_center_y + kTenorBottomYOffset;
      float tenor_y_top = staff_center_y - kTenorTopYOffset;
      for (int l = 0; l < 5; ++l) {
        float ly = tenor_y_bottom - l * line_spacing;
        canvas.drawLine(line_left_x, ly, w, ly, line_paint);
      }
      canvas.drawLine(line_left_x, tenor_y_top, line_left_x, tenor_y_bottom,
                      line_paint);
      DrawCClef(canvas, clef_x, staff_center_y - kTenorStaffCenterOffset,
                kClefScale, kDefaultLineColor);
      DrawTimeSignature(canvas, time_sig_x,
                        staff_center_y - kTenorStaffCenterOffset, line_spacing,
                        beats_per_bar_, note_per_beat_, kDefaultLineColor);
    } else {
      // Clef::TrebleAndBass (Grand Staff)
      float treble_y5 = staff_center_y - kTrebleLine5YOffset;
      float treble_y_top = staff_center_y - kTrebleTopYOffset;
      float bass_y5 = staff_center_y + kBassLine5YOffset;

      for (int l = 0; l < 5; ++l) {
        float ly = treble_y5 - l * line_spacing;
        canvas.drawLine(line_left_x, ly, w, ly, line_paint);
      }
      DrawTrebleClef(canvas, clef_x, treble_y5 - 2.0f * line_spacing,
                     kClefScale, kDefaultLineColor);
      DrawTimeSignature(canvas, time_sig_x, treble_y5 - 2.0f * line_spacing,
                        line_spacing, beats_per_bar_, note_per_beat_,
                        kDefaultLineColor);

      for (int l = 0; l < 5; ++l) {
        float ly = bass_y5 - l * line_spacing;
        canvas.drawLine(line_left_x, ly, w, ly, line_paint);
      }
      DrawBassClef(canvas, clef_x, bass_y5 - 2.0f * line_spacing, kClefScale,
                   kDefaultLineColor);
      DrawTimeSignature(canvas, time_sig_x, bass_y5 - 2.0f * line_spacing,
                        line_spacing, beats_per_bar_, note_per_beat_,
                        kDefaultLineColor);

      canvas.drawLine(line_left_x, treble_y_top, line_left_x, bass_y5,
                      line_paint);
      DrawGrandStaffBrace(canvas, staff_left_x, treble_y_top, bass_y5,
                          kDefaultLineColor);
    }

    // Measure Bar Lines & Numbers
    int ticks_per_bar =
        std::max(kMinTicksPerBar, beats_per_bar_ * kMinTicksPerBar);
    int song_dur_ticks = track_manager_.MsToTicks(
        track_manager_.GetSongDurationMs() + kExtraDurationBufferMs);

    SkPaint bar_paint;
    bar_paint.setColor(kBarLineColor);
    bar_paint.setStrokeWidth(kBarLineStrokeWidth);
    bar_paint.setAntiAlias(true);

    SkPaint num_paint;
    num_paint.setColor(kBarNumberColor);
    num_paint.setAntiAlias(true);
    SkFont* num_font = ::perception::ui::GetUiFont("", kBarNumberFontSize);

    int cur_tick = track_manager_.MsToTicks(cur_time, track_manager_.GetBpm());
    for (int b_idx = 0; b_idx * ticks_per_bar <= song_dur_ticks; ++b_idx) {
      float bx = GetBarLineX(b_idx, cur_tick, cursor_x, pixels_per_tick,
                             beats_per_bar_);

      if (bx >= line_left_x + kBarLineMinXOffset && bx <= w) {
        canvas.drawLine(bx, y_top + kBarLineTopPadding, bx,
                        y_top + staff_h - kBarLineBottomPadding, bar_paint);
        std::string m_str = std::to_string(b_idx + 1);
        canvas.drawString(m_str.c_str(), bx + kBarNumberXOffset,
                          y_top + kBarNumberYPadding, *num_font, num_paint);
      }
    }

    // Render Notes & Rests
    std::vector<NoteSpan> note_spans;
    note_spans.reserve(track.notes.size());
    for (const auto& n : track.notes) {
      note_spans.push_back({n.start_tick, n.duration_ticks, n.key_index});
    }
    int track_last_bar_end_tick =
        GetTrackLastBarEndTick(note_spans, beats_per_bar_, note_per_beat_);

    for (size_t n_idx = 0; n_idx < track.notes.size(); ++n_idx) {
      const auto& note = track.notes[n_idx];
      bool is_hovered =
          (drag_state_ != DragState::None && track.id == drag_track_id_ &&
           static_cast<int>(n_idx) == drag_note_index_) ||
          (track.id == hover_track_id_ &&
           static_cast<int>(n_idx) == hover_note_index_);

      NoteRenderStyle style;
      style.note_color = kDefaultLineColor;
      style.is_hovered = is_hovered;
      style.clef = track.clef;

      if (is_hovered) {
        float nx = GetTickX(note.start_tick, cur_tick, cursor_x,
                            pixels_per_tick, beats_per_bar_);
        float end_x = GetTickX(note.start_tick + note.duration_ticks, cur_tick,
                               cursor_x, pixels_per_tick, beats_per_bar_);
        float dur_px = end_x - nx;

        // Fast-path 2D block rendering (zero decomposition!)
        DrawHoveredNoteBlock(canvas, note.key_index, nx, dur_px, staff_center_y,
                             line_spacing, line_left_x, w, style);
      } else {
        // Zero-allocation callback decomposition!
        DecomposeNote(note.start_tick, note.duration_ticks, beats_per_bar_,
                      note_per_beat_, [&](const NoteSymbolComponent& comp) {
                        DrawSymbolComponentOnStaff(
                            canvas, comp, note.key_index, cursor_x,
                            pixels_per_tick, cur_time, track_manager_.GetBpm(),
                            staff_center_y, line_spacing, line_left_x, w,
                            beats_per_bar_, style);
                      });
      }
    }

    // Render Rests
    if (track_last_bar_end_tick > 0) {
      if (track.clef == Clef::Alto) {
        float alto_staff_center_y = staff_center_y - kAltoStaffCenterOffset;
        auto rests = CalculateStaffRests(note_spans, /*is_treble_staff=*/true,
                                         track_last_bar_end_tick);
        for (const auto& r : rests) {
          NoteRenderStyle rest_style;
          rest_style.note_color = kDefaultLineColor;
          rest_style.clef = track.clef;
          DecomposeNote(
              r.start_tick, r.end_tick - r.start_tick, beats_per_bar_,
              note_per_beat_,
              [&](const NoteSymbolComponent& comp) {
                DrawSymbolComponentOnStaff(
                    canvas, comp, /*key_index=*/kAltoTenorRestKeyIndex,
                    cursor_x, pixels_per_tick, cur_time,
                    track_manager_.GetBpm(), alto_staff_center_y, line_spacing,
                    line_left_x, w, beats_per_bar_, rest_style);
              },
              /*rest=*/true);
        }
      } else if (track.clef == Clef::Tenor) {
        float tenor_staff_center_y = staff_center_y - kTenorStaffCenterOffset;
        auto rests = CalculateStaffRests(note_spans, /*is_treble_staff=*/true,
                                         track_last_bar_end_tick);
        for (const auto& r : rests) {
          NoteRenderStyle rest_style;
          rest_style.note_color = kDefaultLineColor;
          rest_style.clef = track.clef;
          DecomposeNote(
              r.start_tick, r.end_tick - r.start_tick, beats_per_bar_,
              note_per_beat_,
              [&](const NoteSymbolComponent& comp) {
                DrawSymbolComponentOnStaff(
                    canvas, comp, /*key_index=*/kAltoTenorRestKeyIndex,
                    cursor_x, pixels_per_tick, cur_time,
                    track_manager_.GetBpm(), tenor_staff_center_y, line_spacing,
                    line_left_x, w, beats_per_bar_, rest_style);
              },
              /*rest=*/true);
        }
      } else {
        // TrebleAndBass
        float treble_y5 = staff_center_y - kTrebleLine5YOffset;
        float bass_y5 = staff_center_y + kBassLine5YOffset;

        float treble_staff_center_y = treble_y5 - 2.0f * line_spacing;
        auto treble_rests = CalculateStaffRests(
            note_spans, /*is_treble_staff=*/true, track_last_bar_end_tick);
        for (const auto& r : treble_rests) {
          NoteRenderStyle rest_style;
          rest_style.note_color = kDefaultLineColor;
          rest_style.clef = track.clef;
          DecomposeNote(
              r.start_tick, r.end_tick - r.start_tick, beats_per_bar_,
              note_per_beat_,
              [&](const NoteSymbolComponent& comp) {
                DrawSymbolComponentOnStaff(
                    canvas, comp, /*key_index=*/kTrebleRestKeyIndex, cursor_x,
                    pixels_per_tick, cur_time, track_manager_.GetBpm(),
                    treble_staff_center_y, line_spacing, line_left_x, w,
                    beats_per_bar_, rest_style);
              },
              /*rest=*/true);
        }

        float bass_staff_center_y = bass_y5 - 2.0f * line_spacing;
        auto bass_rests = CalculateStaffRests(
            note_spans, /*is_treble_staff=*/false, track_last_bar_end_tick);
        for (const auto& r : bass_rests) {
          NoteRenderStyle rest_style;
          rest_style.note_color = kDefaultLineColor;
          rest_style.clef = track.clef;
          DecomposeNote(
              r.start_tick, r.end_tick - r.start_tick, beats_per_bar_,
              note_per_beat_,
              [&](const NoteSymbolComponent& comp) {
                DrawSymbolComponentOnStaff(
                    canvas, comp, /*key_index=*/kBassRestKeyIndex, cursor_x,
                    pixels_per_tick, cur_time, track_manager_.GetBpm(),
                    bass_staff_center_y, line_spacing, line_left_x, w,
                    beats_per_bar_, rest_style);
              },
              /*rest=*/true);
        }
      }
    }

    // Floating Track Name & Instrument Title
    std::string track_title =
        track.name + " • " + (track.instrument ? track.instrument->name : "");

    float title_y = staff_center_y - kTrebleTitleYOffset;
    if (track.clef == Clef::Alto) {
      title_y = staff_center_y - kAltoTitleYOffset;
    } else if (track.clef == Clef::Tenor) {
      title_y = staff_center_y - kTenorTitleYOffset;
    }

    SkFont* title_font =
        ::perception::ui::GetUiFont("", kTitleFontSize, /*bold=*/true);
    float text_width = title_font->measureText(
        track_title.c_str(), track_title.length(), SkTextEncoding::kUTF8);

    SkPaint title_bg;
    title_bg.setColor(kSheetBackgroundColor);
    canvas.drawRect(
        SkRect::MakeXYWH(line_left_x, title_y - kTitleBgYOffset,
                         text_width + kTitleBgWidthPadding, kTitleBgHeight),
        title_bg);

    SkPaint title_paint;
    title_paint.setColor(track.color);
    title_paint.setAntiAlias(true);
    canvas.drawString(track_title.c_str(), line_left_x + kTitleTextXOffset,
                      title_y + kTitleTextYOffset, *title_font, title_paint);
  }

  // Playback Cursor line
  SkPaint cursor_line;
  cursor_line.setColor(kCursorLineColor);
  cursor_line.setStrokeWidth(kCursorLineStrokeWidth);
  cursor_line.setAntiAlias(true);
  canvas.drawLine(cursor_x, 0, cursor_x, h, cursor_line);

  // Time Display
  double current_time_sec = track_manager_.GetCurrentTimeMs() / kMsPerSecond;
  double song_length_sec = track_manager_.GetSongDurationMs() / kMsPerSecond;
  std::string time_str = perception::FormatTime(current_time_sec) + " / " +
                         perception::FormatTime(song_length_sec);

  SkPaint time_paint;
  time_paint.setColor(kTimeDisplayColor);
  time_paint.setAntiAlias(true);
  SkFont* time_font = ::perception::ui::GetUiFont("", kTimeDisplayFontSize);
  if (time_font) {
    float text_w = time_font->measureText(time_str.c_str(), time_str.size(),
                                          SkTextEncoding::kUTF8);
    canvas.drawString(time_str.c_str(), w - text_w - kTimeDisplayMarginRight,
                      kTimeDisplayY, *time_font, time_paint);
  }
}

}  // namespace notation
