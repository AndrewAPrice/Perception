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

#include "notation/falling_notes_view.h"

#include <algorithm>
#include <cmath>

#include "undo_manager.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/effects/SkGradient.h"
#include "constants.h"
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

// Default vertical scroll speed for falling notes in pixels per millisecond.
constexpr float kDefaultSpeed = 0.12f;

// Bottom margin for the view node in layout units.
constexpr float kLayoutBottomMargin = 4.0f;

// Map indicating whether each semitone index in an octave is a black key (true)
// or white key (false).
constexpr bool kIsBlackKey[kSemitonesPerOctave] = {false, true,  false, false,
                                                   true,  false, true,  false,
                                                   false, true,  false, true};

// Cumulative count of white keys for each semitone index in an octave.
constexpr int kWhiteKeyIndexInOctave[kSemitonesPerOctave] = {0, 1, 1, 2, 3, 3,
                                                             4, 4, 5, 6, 6, 7};

// Bottom offset in pixels from the view height for note vertical positioning.
constexpr float kBottomPadding = 4.0f;

// Width factor relative to white key width for rendering note blocks.
constexpr float kNoteWidthRatio = 0.7f;

// Horizontal center offset (half key width) for white key positioning.
constexpr float kWhiteKeyCenterOffset = 0.5f;

// Note height threshold below which top/bottom resize zones split the note in
// half.
constexpr float kSmallNoteThresholdHeight = 12.0f;

// Distance from note top or bottom edge to trigger vertical resize cursor.
constexpr float kEdgeResizeMargin = 6.0f;

// Stroke width for measure bar lines.
constexpr float kBarLineWidth = 1.5f;

// Stroke width for beat subdivision lines.
constexpr float kBeatLineWidth = 1.0f;

// Stroke width for the canvas bottom line.
constexpr float kBottomLineWidth = 2.0f;

// Offset from bottom of the view for drawing the canvas bottom line.
constexpr float kBottomLineOffset = 2.0f;

// Font size for measure bar numbers.
constexpr float kBarNumberFontSize = 10.0f;

// Horizontal margin from the left edge for measure bar number labels.
constexpr float kBarNumberMarginX = 8.0f;

// Vertical offset above bar lines for measure bar number labels.
constexpr float kBarNumberOffsetY = 3.0f;

// Default release duration in seconds when an instrument is unspecified.
constexpr double kDefaultReleaseDurationSeconds = 0.15;

// Alpha scale factor applied to inactive or unselected track note colors.
constexpr float kInactiveTrackAlphaScale = 0.40f;

// Bit shift count to access the alpha byte in 32-bit ARGB colors.
constexpr uint32 kAlphaShift = 24;

// Bitmask for extracting the 8-bit alpha channel.
constexpr uint32 kAlphaMask = 0xFF;

// Bitmask for extracting 24-bit RGB color channels.
constexpr uint32 kColorRGBMask = 0x00FFFFFF;

// Bottom alpha value for the release fade gradient on selected notes.
constexpr uint32 kSelectedNoteFadeBottomAlpha = 0x60;

// Bottom alpha value for the release fade gradient on unselected notes.
constexpr uint32 kUnselectedNoteFadeBottomAlpha = 0x28;

// Top alpha value for the release fade gradient (fully transparent).
constexpr uint32 kFadeTopAlpha = 0x00000000;

// Corner radius for rounded rectangle note shapes and fade overlays.
constexpr float kNoteCornerRadius = 3.0f;

// Stroke width for the selection highlight border on notes.
constexpr float kHighlightLineWidth = 2.0f;

// Font size for the time display text in the top-right corner.
constexpr float kTimeFontSize = 12.0f;

// Horizontal margin from the right edge for the time display text.
constexpr float kTimeMarginX = 12.0f;

// Vertical offset from top edge for the time display text.
constexpr float kTimeOffsetY = 20.0f;

// Background color for the falling notes canvas.
constexpr uint32 kBackgroundColor = 0xFF181825;

// Color for the canvas bottom line.
constexpr uint32 kBottomLineColor = 0xFF45475A;

// Color for measure bar lines.
constexpr uint32 kBarLineColor = 0xFF585B70;

// Color for beat subdivision lines.
constexpr uint32 kBeatLineColor = 0xFF2A2B3D;

// Color for measure bar number labels.
constexpr uint32 kBarNumberColor = 0xFF7F849C;

// Highlight border color for active or dragged notes.
constexpr uint32 kHighlightBorderColor = 0xFFFDE047;

// Text color for the time overlay display.
constexpr uint32 kTimeTextColor = 0xFFBAC2DE;

int GetWhiteKeyCount(int key_index) {
  int octave = key_index / kSemitonesPerOctave;
  int note = key_index % kSemitonesPerOctave;
  return octave * kDiatonicStepsPerOctave + kWhiteKeyIndexInOctave[note];
}

bool IsKeyBlack(int key_index) {
  return kIsBlackKey[key_index % kSemitonesPerOctave];
}

}  // namespace

FallingNotesView::FallingNotesView(TrackManager& track_manager)
    : NotationView(track_manager, kDefaultSpeed) {
  BuildNode();
}

void FallingNotesView::BuildNode() {
  node_ = Node::Empty(
      [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetWidthPercent(100.0f);
        layout.SetHeightPercent(100.0f);
        layout.SetFlexGrow(1.0f);
        layout.SetFlexShrink(1.0f);
        layout.SetMargin(YGEdgeBottom, kLayoutBottomMargin);
      },
      [this](Node& node) {
        node.OnDraw([this](const DrawContext& context) {
          if (!context.skia_canvas) return;
          context.skia_canvas->save();
          context.skia_canvas->translate(context.area.origin.x,
                                         context.area.origin.y);
          context.skia_canvas->clipRect(
              SkRect::MakeWH(context.area.Width(), context.area.Height()));
          DrawFallingNotes(*context.skia_canvas, context.area.Width(),
                           context.area.Height());

          context.skia_canvas->restore();
        });

        auto focusable = node.GetOrAdd<Focusable>();

        // Mouse hover callback
        node.OnMouseHover([this, focusable](const Point& pt) {
          if (!focusable->HasFocus()) {
            focusable->Focus();
          }
          Size s = node_->GetSize();
          float w = s.width;
          float h = s.height;

          if (space_down_ && !input_focused_ && !track_manager_.IsPlaying()) {
            if (node_->GetCursor() != perception::window::Cursor::Grab) {
              node_->SetCursor(perception::window::Cursor::Grab);
            }
            last_mouse_pos_ = pt;
            if (drag_state_ == DragState::Panning) {
              float eff_speed = speed_ * zoom_level_;
              if (eff_speed <= 0.0f) eff_speed = kDefaultSpeed;
              float dy = pt.y - pan_start_mouse_pos_.y;
              int delta_ms = static_cast<int>(dy / eff_speed);
              int song_dur = track_manager_.GetSongDurationMs();
              int new_time =
                  std::clamp(pan_start_time_ms_ + delta_ms, 0, song_dur);
              track_manager_.Seek(new_time);
              Invalidate();
            }
            return;
          }

          last_mouse_pos_ = pt;

          // Read-only check during playback
          if (track_manager_.IsPlaying()) {
            if (node_->GetCursor() != perception::window::Cursor::Pointer) {
              node_->SetCursor(perception::window::Cursor::Pointer);
            }
            int prev_hover_track = hover_track_id_;
            int prev_hover_note = hover_note_index_;
            hover_track_id_ = 0;
            hover_note_index_ = -1;
            if (prev_hover_track != 0 || prev_hover_note != -1) Invalidate();
            return;
          }

          // Active Drag / Resize / Draw operations
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

            if (drag_state_ == DragState::MovingNote) {
              if (node_->GetCursor() != perception::window::Cursor::Drag) {
                node_->SetCursor(perception::window::Cursor::Drag);
              }
              int target_key = GetKeyIndexAtX(pt.x, w);
              int target_time_ms = GetTimeMsAtY(pt.y, h);
              int target_start_tick =
                  SnapTick(track_manager_.MsToTicks(target_time_ms));

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
            } else if (drag_state_ == DragState::ResizingNoteTop) {
              if (node_->GetCursor() !=
                  perception::window::Cursor::ResizeVertical) {
                node_->SetCursor(perception::window::Cursor::ResizeVertical);
              }
              int target_time_ms = GetTimeMsAtY(pt.y, h);
              int new_end_tick =
                  SnapTick(track_manager_.MsToTicks(target_time_ms));
              new_end_tick = std::max(new_end_tick,
                                      initial_note_start_tick_ + snap_ticks_);
              int new_duration = new_end_tick - initial_note_start_tick_;

              if (track_manager_.CanPlaceNote(drag_track_id_, note.key_index,
                                              initial_note_start_tick_,
                                              new_duration, drag_note_index_)) {
                note.duration_ticks = new_duration;
                track_manager_.SyncTrackNotesMs(*drag_track);
              }
            } else if (drag_state_ == DragState::ResizingNoteBottom) {
              if (node_->GetCursor() !=
                  perception::window::Cursor::ResizeVertical) {
                node_->SetCursor(perception::window::Cursor::ResizeVertical);
              }
              int target_time_ms = GetTimeMsAtY(pt.y, h);
              int end_tick =
                  initial_note_start_tick_ + initial_note_duration_ticks_;
              int new_start_tick =
                  SnapTick(track_manager_.MsToTicks(target_time_ms));
              new_start_tick = std::min(new_start_tick, end_tick - snap_ticks_);
              int new_duration = end_tick - new_start_tick;

              if (track_manager_.CanPlaceNote(drag_track_id_, note.key_index,
                                              new_start_tick, new_duration,
                                              drag_note_index_)) {
                note.start_tick = new_start_tick;
                note.duration_ticks = new_duration;
                track_manager_.SyncTrackNotesMs(*drag_track);
              }
            } else if (drag_state_ == DragState::DrawingNote) {
              int target_key = GetKeyIndexAtX(pt.x, w);
              int target_time_ms = GetTimeMsAtY(pt.y, h);
              int cur_tick = SnapTick(track_manager_.MsToTicks(target_time_ms));
              int start_tick = std::min(draw_start_tick_, cur_tick);
              int end_tick = std::max(draw_start_tick_, cur_tick) + snap_ticks_;
              int dur_ticks = std::max(snap_ticks_, end_tick - start_tick);

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
            if (node_->GetCursor() != perception::window::Cursor::Eraser) {
              node_->SetCursor(perception::window::Cursor::Eraser);
            }
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
              if (hit.is_top_edge || hit.is_bottom_edge) {
                if (node_->GetCursor() !=
                    perception::window::Cursor::ResizeVertical) {
                  node_->SetCursor(perception::window::Cursor::ResizeVertical);
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
            Invalidate();
            return;
          }

          Size s = node_->GetSize();
          float w = s.width;
          float h = s.height;

          // Hit Test Notes
          auto hit = HitTestNotes(pt.x, pt.y, w, h);
          if (hit.note_index >= 0) {
            Track* trk = track_manager_.GetTrack(hit.track_id);
            if (trk && hit.note_index < static_cast<int>(trk->notes.size())) {
              NotifyTrackSelected(hit.track_id, /*auto_scroll=*/false);

              if (control_down_) {
                // Delete Note
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

              if (hit.is_top_edge) {
                drag_state_ = DragState::ResizingNoteTop;
              } else if (hit.is_bottom_edge) {
                drag_state_ = DragState::ResizingNoteBottom;
              } else {
                drag_state_ = DragState::MovingNote;
                TriggerPreviewSound(note.key_index, trk->instrument,
                                    trk->volume, track_manager_.GetBpm());
              }
              Invalidate();
              return;
            }
          }

          // Blank space click -> Place or Draw Note
          if (control_down_) return;

          Track* active_track = track_manager_.GetActiveTrack();
          if (!active_track) return;

          int key_idx = GetKeyIndexAtX(pt.x, w);
          int click_time_ms = GetTimeMsAtY(pt.y, h);
          int start_tick = SnapTick(track_manager_.MsToTicks(click_time_ms));

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

int FallingNotesView::GetKeyIndexAtX(float x, float w) {
  return TrackManager::GetKeyIndexAtX(x, w);
}

int FallingNotesView::GetTimeMsAtY(float y, float h, int view_start_ms,
                                   float eff_speed) {
  return TrackManager::GetTimeMsAtY(y, h, view_start_ms, eff_speed);
}

int FallingNotesView::GetTimeMsAtY(float y, float h) {
  float eff_speed = speed_ * zoom_level_;
  int view_start_ms = track_manager_.GetCurrentTimeMs();
  return GetTimeMsAtY(y, h, view_start_ms, eff_speed);
}

FallingNotesView::NoteHitResult FallingNotesView::HitTestNotes(float x, float y,
                                                               float w,
                                                               float h) {
  NoteHitResult result;
  float white_key_w = w / kTotalWhiteKeys;
  int cur_time = track_manager_.GetCurrentTimeMs();
  float speed = speed_ * zoom_level_;

  int active_track_id = track_manager_.GetActiveTrackId();
  const auto& tracks = track_manager_.GetTracks();
  for (int t_idx = static_cast<int>(tracks.size()) - 1; t_idx >= 0; --t_idx) {
    const auto& track = tracks[t_idx];
    if (track.id != active_track_id || track.muted || track.hidden) continue;

    for (int n_idx = static_cast<int>(track.notes.size()) - 1; n_idx >= 0;
         --n_idx) {
      const auto& note = track.notes[n_idx];
      int start_t = note.start_time_ms;
      int dur_t = note.duration_ms;

      float dt_start = static_cast<float>(start_t - cur_time);
      float dt_end = static_cast<float>((start_t + dur_t) - cur_time);

      float y_bottom = (h - kBottomPadding) - (dt_start * speed);
      float y_top = (h - kBottomPadding) - (dt_end * speed);

      if (y_bottom >= 0 && y_top <= h) {
        float x_center = 0.0f;
        int key = note.key_index;
        int w_count = GetWhiteKeyCount(key);

        if (IsKeyBlack(key)) {
          x_center = w_count * white_key_w;
        } else {
          x_center = (w_count + kWhiteKeyCenterOffset) * white_key_w;
        }

        float note_w = white_key_w * kNoteWidthRatio;
        float rect_l = x_center - (note_w / 2.0f);
        float rect_r = rect_l + note_w;
        float rect_t = y_top;
        float rect_b = y_bottom;

        if (x >= rect_l && x <= rect_r && y >= rect_t && y <= rect_b) {
          result.track_id = track.id;
          result.note_index = n_idx;

          float height = rect_b - rect_t;
          if (height < kSmallNoteThresholdHeight) {
            float mid = rect_t + (height / 2.0f);
            if (y <= mid) {
              result.is_top_edge = true;
            } else {
              result.is_bottom_edge = true;
            }
          } else {
            if (y - rect_t <= kEdgeResizeMargin) {
              result.is_top_edge = true;
            } else if (rect_b - y <= kEdgeResizeMargin) {
              result.is_bottom_edge = true;
            }
          }
          return result;
        }
      }
    }
  }

  return result;
}

void FallingNotesView::DrawFallingNotes(SkCanvas& canvas, float w, float h) {
  // Dark sheet background
  SkPaint bg_paint;
  bg_paint.setColor(kBackgroundColor);
  canvas.drawRect(SkRect::MakeXYWH(0, 0, w, h), bg_paint);

  // Grid line paints
  SkPaint line_paint;
  line_paint.setColor(kBottomLineColor);
  line_paint.setStrokeWidth(kBottomLineWidth);
  canvas.drawLine(0, h - kBottomLineOffset, w, h - kBottomLineOffset,
                  line_paint);

  float white_key_w = w / kTotalWhiteKeys;
  int cur_time = track_manager_.GetCurrentTimeMs();
  float speed = speed_ * zoom_level_;

  // Measure bar lines & beat lines
  int beats_per_bar = std::max(1, beats_per_bar_);
  int ticks_per_bar = beats_per_bar * kTicksPerBeat;

  float safe_speed = speed > 0.0f ? speed : kDefaultSpeed;
  int top_time_ms = cur_time + static_cast<int>(h / safe_speed);
  int bottom_time_ms = cur_time - static_cast<int>(kBottomPadding / safe_speed);

  int start_tick =
      std::max(0, track_manager_.MsToTicks(std::max(0, bottom_time_ms)));
  int end_tick = track_manager_.MsToTicks(top_time_ms) + ticks_per_bar;

  int start_bar = start_tick / ticks_per_bar;
  int end_bar = (end_tick / ticks_per_bar) + 1;

  SkPaint bar_line_paint;
  bar_line_paint.setColor(kBarLineColor);
  bar_line_paint.setStrokeWidth(kBarLineWidth);
  bar_line_paint.setAntiAlias(true);

  SkPaint beat_line_paint;
  beat_line_paint.setColor(kBeatLineColor);
  beat_line_paint.setStrokeWidth(kBeatLineWidth);
  beat_line_paint.setAntiAlias(true);

  SkPaint bar_num_paint;
  bar_num_paint.setColor(kBarNumberColor);
  bar_num_paint.setAntiAlias(true);
  static SkFont* bar_num_font =
      ::perception::ui::GetUiFont("", kBarNumberFontSize);

  for (int b = start_bar; b <= end_bar; ++b) {
    int bar_tick = b * ticks_per_bar;
    int bar_ms = track_manager_.TicksToMs(bar_tick);

    // Sub-bar beat lines
    for (int k = 1; k < beats_per_bar; ++k) {
      int beat_tick = bar_tick + k * kTicksPerBeat;
      int beat_ms = track_manager_.TicksToMs(beat_tick);
      float dt_beat = static_cast<float>(beat_ms - cur_time);
      float y_beat = (h - kBottomPadding) - (dt_beat * speed);
      if (y_beat >= 0.0f && y_beat <= h - kBottomPadding) {
        canvas.drawLine(0.0f, y_beat, w, y_beat, beat_line_paint);
      }
    }

    // Measure bar line
    float dt_bar = static_cast<float>(bar_ms - cur_time);
    float y_bar = (h - kBottomPadding) - (dt_bar * speed);
    if (y_bar >= 0.0f && y_bar <= h - kBottomPadding) {
      canvas.drawLine(0.0f, y_bar, w, y_bar, bar_line_paint);
      std::string label = std::to_string(b + 1);
      canvas.drawString(label.c_str(), kBarNumberMarginX,
                        y_bar - kBarNumberOffsetY, *bar_num_font,
                        bar_num_paint);
    }
  }

  auto draw_track_notes = [&](const Track& track, bool is_selected) {
    if (track.muted || track.hidden) return;

    uint32 track_color = track.color;
    if (!is_selected) {
      // Scale alpha for semi-transparent background notes
      uint32 alpha =
          static_cast<uint32>(((track_color >> kAlphaShift) & kAlphaMask) *
                              kInactiveTrackAlphaScale);
      track_color = (track_color & kColorRGBMask) | (alpha << kAlphaShift);
    }

    SkPaint note_paint;
    note_paint.setColor(track_color);
    note_paint.setAntiAlias(true);

    for (size_t i = 0; i < track.notes.size(); ++i) {
      const auto& note = track.notes[i];
      const Instrument* inst =
          track.instrument ? track.instrument : GetDefaultInstrument();
      double release_sec = inst ? inst->release_duration_seconds
                                : kDefaultReleaseDurationSeconds;
      int release_ms = static_cast<int>(std::round(release_sec * kMsPerSecond));

      int start_t = note.start_time_ms;
      int dur_t = note.duration_ms;

      float dt_start = static_cast<float>(start_t - cur_time);
      float dt_end = static_cast<float>((start_t + dur_t) - cur_time);
      float dt_fade =
          static_cast<float>((start_t + dur_t + release_ms) - cur_time);

      float y_bottom = (h - kBottomPadding) - (dt_start * speed);
      float y_top = (h - kBottomPadding) - (dt_end * speed);
      float y_fade_top = (h - kBottomPadding) - (dt_fade * speed);

      if (y_bottom >= 0 && y_fade_top <= h) {
        float x_center = 0.0f;
        int key = note.key_index;
        int w_count = GetWhiteKeyCount(key);

        if (IsKeyBlack(key)) {
          x_center = w_count * white_key_w;
        } else {
          x_center = (w_count + kWhiteKeyCenterOffset) * white_key_w;
        }

        float note_w = white_key_w * kNoteWidthRatio;
        float rect_l = x_center - (note_w / 2.0f);
        float rect_r = rect_l + note_w;

        // Faint fade-out shadow above note
        float fade_t = std::max(0.0f, y_fade_top);
        float fade_b = std::min(h - kBottomPadding, y_top);

        if (fade_b > fade_t) {
          SkPaint fade_paint;
          fade_paint.setAntiAlias(true);

          SkPoint pts[2] = {SkPoint::Make(rect_l, fade_b),
                            SkPoint::Make(rect_l, fade_t)};
          uint32 bottom_alpha = is_selected ? kSelectedNoteFadeBottomAlpha
                                            : kUnselectedNoteFadeBottomAlpha;
          uint32 color_bottom =
              (track.color & kColorRGBMask) | (bottom_alpha << kAlphaShift);
          uint32 color_top = (track.color & kColorRGBMask) | kFadeTopAlpha;

          SkColor4f colors[2] = {SkColor4f::FromColor(color_bottom),
                                 SkColor4f::FromColor(color_top)};
          SkGradient gradient(SkGradient::Colors(colors, SkTileMode::kClamp),
                              SkGradient::Interpolation());
          sk_sp<SkShader> shader = SkShaders::LinearGradient(pts, gradient);
          fade_paint.setShader(shader);

          SkRRect fade_rrect;
          fade_rrect.setRectXY(SkRect::MakeLTRB(rect_l, fade_t, rect_r, fade_b),
                               kNoteCornerRadius, kNoteCornerRadius);
          canvas.drawRRect(fade_rrect, fade_paint);
        }

        // Main note body
        float rect_t = std::max(0.0f, y_top);
        float rect_b = std::min(h - kBottomPadding, y_bottom);

        if (rect_b > rect_t) {
          SkRRect rrect;
          rrect.setRectXY(SkRect::MakeLTRB(rect_l, rect_t, rect_r, rect_b),
                          kNoteCornerRadius, kNoteCornerRadius);

          bool is_highlighted =
              is_selected &&
              ((drag_state_ != DragState::None && track.id == drag_track_id_ &&
                static_cast<int>(i) == drag_note_index_) ||
               (track.id == hover_track_id_ &&
                static_cast<int>(i) == hover_note_index_));

          if (is_highlighted) {
            SkPaint border_paint;
            border_paint.setColor(kHighlightBorderColor);
            border_paint.setStyle(SkPaint::kStroke_Style);
            border_paint.setStrokeWidth(kHighlightLineWidth);
            canvas.drawRRect(rrect, note_paint);
            canvas.drawRRect(rrect, border_paint);
          } else {
            canvas.drawRRect(rrect, note_paint);
          }
        }
      }
    }
  };

  int active_track_id = track_manager_.GetActiveTrackId();

  // Pass 1: Render non-selected tracks first, semi-transparent
  for (const auto& track : track_manager_.GetTracks()) {
    if (track.id != active_track_id)
      draw_track_notes(track, /*is_selected=*/false);
  }

  // Pass 2: Render selected track opaquely on top
  const Track* active_track = track_manager_.GetTrack(active_track_id);
  if (active_track) {
    draw_track_notes(*active_track, /*is_selected=*/true);
  }

  // Draw current time / total song duration in top right corner
  double current_time_sec = track_manager_.GetCurrentTimeMs() / kMsPerSecond;
  double song_length_sec = track_manager_.GetSongDurationMs() / kMsPerSecond;
  std::string time_str = perception::FormatTime(current_time_sec) + " / " +
                         perception::FormatTime(song_length_sec);

  SkPaint time_paint;
  time_paint.setColor(kTimeTextColor);
  time_paint.setAntiAlias(true);
  static SkFont* time_font = perception::ui::GetUiFont("", kTimeFontSize);
  if (time_font) {
    float text_w = time_font->measureText(time_str.c_str(), time_str.size(),
                                          SkTextEncoding::kUTF8);
    canvas.drawString(time_str.c_str(), w - text_w - kTimeMarginX, kTimeOffsetY,
                      *time_font, time_paint);
  }
}

}  // namespace notation
