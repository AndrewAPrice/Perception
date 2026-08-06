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

#include "panels/timeline_ruler.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/window/cursor.h"
#include "perception/window/mouse_button.h"

using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::Size;

namespace panels {

namespace window = ::perception::window;


TimelineRuler::TimelineRuler(TrackManager& track_manager,
                             std::function<void()> on_seek)
    : track_manager_(track_manager), on_seek_(std::move(on_seek)) {
  BuildUI();
}

void TimelineRuler::BuildUI() {
  node_ = Node::Empty(
      [](Layout& layout) {
        layout.SetWidthPercent(100.0f);
        layout.SetHeight(18.0f);
        layout.SetFlexShrink(0.0f);
      },
      [this](Node& node) {
        node.OnDraw([this, &node](const DrawContext& context) {
          if (!context.skia_canvas) return;
          SkCanvas& canvas = *context.skia_canvas;
          canvas.save();
          canvas.translate(context.area.origin.x, context.area.origin.y);
          Size size{context.area.Width(), context.area.Height()};
          Draw(canvas, size);
          canvas.restore();
        });


        node.SetCursor(window::Cursor::ResizeHorizontal);

        node.OnMouseButtonDown(
            [this, &node](const Point& pt, window::MouseButton button) {
              is_scrubbing_ = true;
              Size s = node.GetSize();
              HandleMouseScrub(pt.x, s.width);
            });

        node.OnMouseHover([this, &node](const Point& pt) {
          if (is_scrubbing_) {
            Size s = node.GetSize();
            HandleMouseScrub(pt.x, s.width);
          }
        });

        node.OnMouseButtonUp(
            [this, &node](const Point& pt, window::MouseButton button) {
              if (is_scrubbing_) {
                Size s = node.GetSize();
                HandleMouseScrub(pt.x, s.width);
                is_scrubbing_ = false;
              }
            });
      });
}

void TimelineRuler::Invalidate() {
  if (node_) {
    node_->Invalidate();
  }
}

void TimelineRuler::HandleMouseScrub(float x, float width) {
  if (width <= 0.0f) return;
  int duration_ms = std::max(30000, track_manager_.GetSongDurationMs());
  float clamped_x = std::clamp(x, 0.0f, width);
  int target_ms = static_cast<int>((clamped_x / width) * duration_ms);
  track_manager_.Seek(target_ms);
  if (on_seek_) {
    on_seek_();
  }
  Invalidate();
}

void TimelineRuler::Draw(SkCanvas& canvas, const Size& size) {
  float w = size.width;
  float h = size.height;
  if (w <= 0.0f || h <= 0.0f) return;

  // Background
  SkPaint bg_paint;
  bg_paint.setColor(0xFF181825);
  canvas.drawRect(SkRect::MakeXYWH(0, 0, w, h), bg_paint);

  // Bottom border line
  SkPaint border_paint;
  border_paint.setColor(0xFF313244);
  border_paint.setStrokeWidth(1.0f);
  canvas.drawLine(0.0f, h - 0.5f, w, h - 0.5f, border_paint);

  int duration_ms = std::max(30000, track_manager_.GetSongDurationMs());
  int beats_per_bar = std::max(1, beats_per_bar_);
  int ticks_per_bar = beats_per_bar * 64;
  int ms_per_bar = track_manager_.TicksToMs(ticks_per_bar);
  if (ms_per_bar <= 0) ms_per_bar = 2000;

  int total_bars = std::max(16, (duration_ms + ms_per_bar - 1) / ms_per_bar);

  SkPaint bar_line_paint;
  bar_line_paint.setColor(0xFF585B70);
  bar_line_paint.setStrokeWidth(1.0f);
  bar_line_paint.setAntiAlias(true);

  SkPaint beat_line_paint;
  beat_line_paint.setColor(0xFF313244);
  beat_line_paint.setStrokeWidth(1.0f);
  beat_line_paint.setAntiAlias(true);

  SkPaint text_paint;
  text_paint.setColor(0xFF9399B2);
  text_paint.setAntiAlias(true);
  for (int b = 0; b <= total_bars; ++b) {
    int bar_ms = b * ms_per_bar;
    float bar_x =
        (static_cast<float>(bar_ms) / static_cast<float>(duration_ms)) * w;
    if (bar_x > w) break;

    // Draw main measure tick line (taller)
    canvas.drawLine(bar_x, 4.0f, bar_x, h - 1.0f, bar_line_paint);

    // Draw sub-beat ticks (shorter)
    for (int k = 1; k < beats_per_bar; ++k) {
      int beat_ms = bar_ms + track_manager_.TicksToMs(k * 64);
      float beat_x =
          (static_cast<float>(beat_ms) / static_cast<float>(duration_ms)) * w;
      if (beat_x >= 0.0f && beat_x <= w) {
        canvas.drawLine(beat_x, 12.0f, beat_x, h - 1.0f, beat_line_paint);
      }
    }
  }

  // Draw Playhead Line & Handle
  int cur_time_ms = track_manager_.GetCurrentTimeMs();
  float playhead_x =
      (static_cast<float>(cur_time_ms) / static_cast<float>(duration_ms)) * w;
  playhead_x = std::clamp(playhead_x, 0.0f, w);

  SkPaint playhead_paint;
  playhead_paint.setColor(0xFFEF4444);
  playhead_paint.setStrokeWidth(2.0f);
  playhead_paint.setAntiAlias(true);

  // Playhead vertical line
  canvas.drawLine(playhead_x, 0.0f, playhead_x, h, playhead_paint);

  // Playhead top triangle handle
  SkPaint handle_paint;
  handle_paint.setColor(0xFFEF4444);
  handle_paint.setAntiAlias(true);

  SkPathBuilder builder;
  builder.moveTo(playhead_x - 5.0f, 0.0f);
  builder.lineTo(playhead_x + 5.0f, 0.0f);
  builder.lineTo(playhead_x, 8.0f);
  builder.close();
  canvas.drawPath(builder.detach(), handle_paint);
}


}  // namespace panels
