// Copyright 2026 Google LLC.
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

#include "plotters.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "gui.h"
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkString.h"
#include "include/effects/SkDashPathEffect.h"
#include "perception/ui/font.h"

extern "C" {
#include "utils/errors.h"
#include "netsurf/bitmap.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"
}

namespace netsurf {
namespace perception {
namespace {

SkCanvas* active_canvas = nullptr;
int canvas_save_count = 0;

SkColor ConvertColor(colour color) {
  if (color == NS_TRANSPARENT) return SK_ColorTRANSPARENT;
  uint8_t red = (color) & 0xff;
  uint8_t green = (color >> 8) & 0xff;
  uint8_t blue = (color >> 16) & 0xff;
  uint8_t alpha = 255 - ((color >> 24) & 0xff);
  return SkColorSetARGB(alpha, red, green, blue);
}

void ConfigureStrokePaint(SkPaint& paint, const plot_style_t& style) {
  paint.setColor(ConvertColor(style.stroke_type != PLOT_OP_TYPE_NONE
                                  ? style.stroke_colour
                                  : style.fill_colour));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setAntiAlias(true);
  float stroke_width =
      std::max(1.0f, plot_style_fixed_to_float(style.stroke_width));
  paint.setStrokeWidth(stroke_width);
  if (style.stroke_type == PLOT_OP_TYPE_DOT) {
    const SkScalar intervals[] = {stroke_width, stroke_width};
    paint.setPathEffect(SkDashPathEffect::Make(intervals, 0.0f));
  } else if (style.stroke_type == PLOT_OP_TYPE_DASH) {
    const SkScalar intervals[] = {3.0f * stroke_width, 3.0f * stroke_width};
    paint.setPathEffect(SkDashPathEffect::Make(intervals, 0.0f));
  } else {
    paint.setPathEffect(nullptr);
  }
}

float TransformX(const float transform[6], float px, float py) {
  if (!transform) return px;
  return transform[0] * px + transform[2] * py + transform[4];
}

float TransformY(const float transform[6], float px, float py) {
  if (!transform) return py;
  return transform[1] * px + transform[3] * py + transform[5];
}

nserror PlotClip(const struct redraw_context* ctx, const struct rect* clip) {
  if (!active_canvas || !clip) return NSERROR_INVALID;
  active_canvas->restoreToCount(canvas_save_count);
  active_canvas->save();
  active_canvas->clipRect(SkRect::MakeLTRB(
      static_cast<float>(clip->x0), static_cast<float>(clip->y0),
      static_cast<float>(clip->x1), static_cast<float>(clip->y1)));
  return NSERROR_OK;
}

nserror PlotArc(const struct redraw_context* ctx, const plot_style_t* style,
                int x_coord, int y_coord, int radius, int angle1, int angle2) {
  if (!active_canvas || !style) return NSERROR_INVALID;
  if (style->stroke_type == PLOT_OP_TYPE_NONE &&
      style->fill_type == PLOT_OP_TYPE_NONE)
    return NSERROR_OK;

  SkPaint paint;
  ConfigureStrokePaint(paint, *style);

  if (angle2 < angle1) angle2 += 360;

  SkRect oval = SkRect::MakeLTRB(
      static_cast<float>(x_coord - radius),
      static_cast<float>(y_coord - radius),
      static_cast<float>(x_coord + radius),
      static_cast<float>(y_coord + radius));
  float start = static_cast<float>(-angle1);
  float sweep = static_cast<float>(-(angle2 - angle1));
  active_canvas->drawArc(oval, start, sweep, false, paint);
  return NSERROR_OK;
}

nserror PlotDisc(const struct redraw_context* ctx, const plot_style_t* style,
                 int x_coord, int y_coord, int radius) {
  if (!active_canvas || !style) return NSERROR_INVALID;

  if (style->fill_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(style->fill_colour));
    paint.setStyle(SkPaint::kFill_Style);
    active_canvas->drawCircle(static_cast<float>(x_coord),
                              static_cast<float>(y_coord),
                              static_cast<float>(radius), paint);
  }

  if (style->stroke_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    ConfigureStrokePaint(paint, *style);
    active_canvas->drawCircle(static_cast<float>(x_coord),
                              static_cast<float>(y_coord),
                              static_cast<float>(radius), paint);
  }
  return NSERROR_OK;
}

nserror PlotLine(const struct redraw_context* ctx, const plot_style_t* style,
                 const struct rect* line) {
  if (!active_canvas || !style || !line) return NSERROR_INVALID;
  if (style->stroke_type == PLOT_OP_TYPE_NONE) return NSERROR_OK;

  SkPaint paint;
  ConfigureStrokePaint(paint, *style);

  bool is_horizontal = (line->y0 == line->y1);
  bool is_vertical = (line->x0 == line->x1);

  if (is_horizontal || is_vertical) {
    paint.setAntiAlias(false);
    float offset = 0.5f;
    int stroke_width_int =
        static_cast<int>(std::round(paint.getStrokeWidth()));
    if (stroke_width_int % 2 == 0) offset = 0.0f;
    if (is_horizontal) {
      float y_coord = static_cast<float>(line->y0) + offset;
      active_canvas->drawLine(static_cast<float>(line->x0), y_coord,
                              static_cast<float>(line->x1), y_coord, paint);
      return NSERROR_OK;
    }
    float x_coord = static_cast<float>(line->x0) + offset;
    active_canvas->drawLine(x_coord, static_cast<float>(line->y0),
                            x_coord, static_cast<float>(line->y1), paint);
    return NSERROR_OK;
  }

  active_canvas->drawLine(static_cast<float>(line->x0),
                          static_cast<float>(line->y0),
                          static_cast<float>(line->x1),
                          static_cast<float>(line->y1), paint);
  return NSERROR_OK;
}

nserror PlotRectangle(const struct redraw_context* ctx,
                      const plot_style_t* style, const struct rect* rect) {
  if (!active_canvas || !style || !rect) return NSERROR_INVALID;
  SkRect sk_rect = SkRect::MakeLTRB(
      static_cast<float>(rect->x0), static_cast<float>(rect->y0),
      static_cast<float>(rect->x1), static_cast<float>(rect->y1));

  if (style->fill_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(false);
    paint.setColor(ConvertColor(style->fill_colour));
    paint.setStyle(SkPaint::kFill_Style);
    active_canvas->drawRect(sk_rect, paint);
  }

  if (style->stroke_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    ConfigureStrokePaint(paint, *style);
    paint.setAntiAlias(false);
    SkRect stroke_rect = sk_rect;
    int stroke_width_int =
        static_cast<int>(std::round(paint.getStrokeWidth()));
    if (stroke_width_int % 2 != 0) stroke_rect.inset(0.5f, 0.5f);
    active_canvas->drawRect(stroke_rect, paint);
  }
  return NSERROR_OK;
}

nserror PlotPolygon(const struct redraw_context* ctx, const plot_style_t* style,
                    const int* points, unsigned int point_count) {
  if (!active_canvas || !style || !points || point_count < 3)
    return NSERROR_INVALID;

  std::vector<SkPoint> pts;
  pts.reserve(point_count);
  for (unsigned int i = 0; i < point_count; ++i) {
    pts.push_back(SkPoint::Make(static_cast<float>(points[i * 2]),
                                static_cast<float>(points[i * 2 + 1])));
  }

  SkPath path =
      SkPath::Polygon(SkSpan<const SkPoint>(pts.data(), point_count), true);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(style->fill_colour));
  paint.setStyle(SkPaint::kFill_Style);
  active_canvas->drawPath(path, paint);
  return NSERROR_OK;
}

nserror PlotPath(const struct redraw_context* ctx, const plot_style_t* pstyle,
                 const float* points, unsigned int point_count,
                 const float transform[6]) {
  if (!active_canvas || !pstyle || !points) return NSERROR_INVALID;
  if (point_count == 0) return NSERROR_OK;
  if (static_cast<int>(points[0]) != PLOTTER_PATH_MOVE) return NSERROR_INVALID;

  SkPathBuilder builder;
  builder.setFillType(SkPathFillType::kWinding);
  bool empty_path = true;
  for (unsigned int i = 0; i < point_count;) {
    int cmd = static_cast<int>(points[i]);
    if (cmd == PLOTTER_PATH_MOVE) {
      if (i + 2 >= point_count) return NSERROR_INVALID;
      builder.moveTo(TransformX(transform, points[i + 1], points[i + 2]),
                     TransformY(transform, points[i + 1], points[i + 2]));
      i += 3;
    } else if (cmd == PLOTTER_PATH_CLOSE) {
      if (!empty_path) builder.close();
      i++;
    } else if (cmd == PLOTTER_PATH_LINE) {
      if (i + 2 >= point_count) return NSERROR_INVALID;
      builder.lineTo(TransformX(transform, points[i + 1], points[i + 2]),
                     TransformY(transform, points[i + 1], points[i + 2]));
      i += 3;
      empty_path = false;
    } else if (cmd == PLOTTER_PATH_BEZIER) {
      if (i + 6 >= point_count) return NSERROR_INVALID;
      builder.cubicTo(TransformX(transform, points[i + 1], points[i + 2]),
                      TransformY(transform, points[i + 1], points[i + 2]),
                      TransformX(transform, points[i + 3], points[i + 4]),
                      TransformY(transform, points[i + 3], points[i + 4]),
                      TransformX(transform, points[i + 5], points[i + 6]),
                      TransformY(transform, points[i + 5], points[i + 6]));
      i += 7;
      empty_path = false;
    } else {
      return NSERROR_INVALID;
    }
  }

  if (empty_path) return NSERROR_OK;

  SkPath path = builder.detach();

  if (pstyle->fill_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(pstyle->fill_colour));
    paint.setStyle(SkPaint::kFill_Style);
    active_canvas->drawPath(path, paint);
  }

  if (pstyle->stroke_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    ConfigureStrokePaint(paint, *pstyle);
    active_canvas->drawPath(path, paint);
  }

  return NSERROR_OK;
}

nserror PlotText(const struct redraw_context* ctx,
                 const struct plot_font_style* fstyle, int x_coord, int y_coord,
                 const char* text, size_t length) {
  if (!active_canvas || !fstyle || !text) return NSERROR_INVALID;
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) return NSERROR_INVALID;
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(fstyle->foreground));
  active_canvas->drawString(SkString(text, length),
                            static_cast<float>(x_coord),
                            static_cast<float>(y_coord), *font, paint);
  return NSERROR_OK;
}

nserror PlotBitmap(const struct redraw_context* ctx, struct bitmap* bitmap,
                   int x_coord, int y_coord, int width, int height,
                   colour bg_colour, bitmap_flags_t flags) {
  if (!active_canvas || !bitmap) return NSERROR_INVALID;
  if (width <= 0 || height <= 0) return NSERROR_OK;

  if (!bitmap->cached_image)
    bitmap->cached_image = SkImages::RasterFromBitmap(bitmap->sk_bitmap);
  if (!bitmap->cached_image) return NSERROR_INVALID;

  SkRect clip = active_canvas->getLocalClipBounds();

  float start_x = static_cast<float>(x_coord);
  float end_x = static_cast<float>(x_coord + width);
  if ((flags & BITMAPF_REPEAT_X) != 0) {
    start_x = static_cast<float>(x_coord) +
              std::floor((clip.left() - static_cast<float>(x_coord)) /
                         static_cast<float>(width)) *
                  static_cast<float>(width);
    end_x = clip.right();
  }

  float start_y = static_cast<float>(y_coord);
  float end_y = static_cast<float>(y_coord + height);
  if ((flags & BITMAPF_REPEAT_Y) != 0) {
    start_y = static_cast<float>(y_coord) +
              std::floor((clip.top() - static_cast<float>(y_coord)) /
                         static_cast<float>(height)) *
                  static_cast<float>(height);
    end_y = clip.bottom();
  }

  if (end_x <= clip.left() || start_x >= clip.right() ||
      end_y <= clip.top() || start_y >= clip.bottom()) {
    return NSERROR_OK;
  }

  SkSamplingOptions sampling =
      (width == bitmap->sk_bitmap.width() &&
       height == bitmap->sk_bitmap.height())
          ? SkSamplingOptions(SkFilterMode::kNearest)
          : SkSamplingOptions(SkFilterMode::kLinear, SkMipmapMode::kLinear);

  for (float cur_y = start_y; cur_y < end_y;
       cur_y += static_cast<float>(height)) {
    if (cur_y + static_cast<float>(height) <= clip.top() ||
        cur_y >= clip.bottom()) {
      continue;
    }
    for (float cur_x = start_x; cur_x < end_x;
         cur_x += static_cast<float>(width)) {
      if (cur_x + static_cast<float>(width) <= clip.left() ||
          cur_x >= clip.right()) {
        continue;
      }
      SkRect dest = SkRect::MakeXYWH(cur_x, cur_y, static_cast<float>(width),
                                     static_cast<float>(height));
      active_canvas->drawImageRect(bitmap->cached_image, dest, sampling);
    }
  }
  return NSERROR_OK;
}

}  // namespace

void SetActiveCanvas(SkCanvas* canvas) {
  active_canvas = canvas;
  canvas_save_count = canvas ? canvas->getSaveCount() : 0;
}
SkCanvas* GetActiveCanvas() { return active_canvas; }

const struct plotter_table skia_plotters = {
    .clip = PlotClip,
    .arc = PlotArc,
    .disc = PlotDisc,
    .line = PlotLine,
    .rectangle = PlotRectangle,
    .polygon = PlotPolygon,
    .path = PlotPath,
    .bitmap = PlotBitmap,
    .text = PlotText,
    .option_knockout = false,
};

}  // namespace perception
}  // namespace netsurf
