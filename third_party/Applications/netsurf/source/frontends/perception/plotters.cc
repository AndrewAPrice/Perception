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

#include <cmath>
#include <iostream>
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
#include "include/core/SkString.h"
#include "perception/ui/font.h"

extern "C" {
#include "netsurf/bitmap.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"
}

namespace netsurf {
namespace perception {
namespace {

SkCanvas* active_canvas = nullptr;

/* Color Conversion */
SkColor ConvertColor(colour c) {
  uint8_t r = (c) & 0xff;
  uint8_t g = (c >> 8) & 0xff;
  uint8_t b = (c >> 16) & 0xff;
  uint8_t a = 255;
  if (c == NS_TRANSPARENT) {
    a = 0;
  } else if ((c & 0xff000000) != 0) {
    a = (c >> 24) & 0xff;
  }
  return SkColorSetARGB(a, r, g, b);
}

nserror PlotClip(const struct redraw_context* ctx, const struct rect* clip) {
  if (!active_canvas) return NSERROR_INVALID;
  active_canvas->restore();
  active_canvas->save();
  active_canvas->clipRect(SkRect::MakeLTRB((float)clip->x0, (float)clip->y0,
                                           (float)clip->x1, (float)clip->y1));
  return NSERROR_OK;
}

nserror PlotArc(const struct redraw_context* ctx, const plot_style_t* style,
                int x, int y, int radius, int angle1, int angle2) {
  if (!active_canvas) return NSERROR_INVALID;
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(style->stroke_type != PLOT_OP_TYPE_NONE
                                  ? style->stroke_colour
                                  : style->fill_colour));
  paint.setStyle(SkPaint::kStroke_Style);
  paint.setStrokeWidth(plot_style_fixed_to_float(style->stroke_width));

  SkRect oval = SkRect::MakeLTRB((float)(x - radius), (float)(y - radius),
                                 (float)(x + radius), (float)(y + radius));
  float start = (float)angle1;
  float sweep = (float)(angle2 - angle1);
  active_canvas->drawArc(oval, start, sweep, false, paint);
  return NSERROR_OK;
}

nserror PlotDisc(const struct redraw_context* ctx, const plot_style_t* style,
                 int x, int y, int radius) {
  if (!active_canvas) return NSERROR_INVALID;

  if (style->fill_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(style->fill_colour));
    paint.setStyle(SkPaint::kFill_Style);
    active_canvas->drawCircle((float)x, (float)y, (float)radius, paint);
  }

  if (style->stroke_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(style->stroke_colour));
    paint.setStrokeWidth(plot_style_fixed_to_float(style->stroke_width));
    paint.setStyle(SkPaint::kStroke_Style);
    active_canvas->drawCircle((float)x, (float)y, (float)radius, paint);
  }
  return NSERROR_OK;
}

nserror PlotLine(const struct redraw_context* ctx, const plot_style_t* style,
                 const struct rect* line) {
  if (!active_canvas) return NSERROR_INVALID;
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(style->stroke_colour));
  paint.setStrokeWidth(plot_style_fixed_to_float(style->stroke_width));
  paint.setStyle(SkPaint::kStroke_Style);
  active_canvas->drawLine((float)line->x0, (float)line->y0, (float)line->x1,
                          (float)line->y1, paint);
  return NSERROR_OK;
}

nserror PlotRectangle(const struct redraw_context* ctx,
                      const plot_style_t* style, const struct rect* rect) {
  if (!active_canvas) return NSERROR_INVALID;
  SkRect sk_rect = SkRect::MakeLTRB((float)rect->x0, (float)rect->y0,
                                    (float)rect->x1, (float)rect->y1);

  if (style->fill_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(style->fill_colour));
    paint.setStyle(SkPaint::kFill_Style);
    active_canvas->drawRect(sk_rect, paint);
  }

  if (style->stroke_type != PLOT_OP_TYPE_NONE) {
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(style->stroke_colour));
    paint.setStrokeWidth(plot_style_fixed_to_float(style->stroke_width));
    paint.setStyle(SkPaint::kStroke_Style);
    active_canvas->drawRect(sk_rect, paint);
  }
  return NSERROR_OK;
}

nserror PlotPolygon(const struct redraw_context* ctx, const plot_style_t* style,
                    const int* p, unsigned int n) {
  if (!active_canvas || n < 3) return NSERROR_INVALID;

  std::vector<SkPoint> pts;
  for (unsigned int i = 0; i < n; ++i) {
    pts.push_back(SkPoint::Make((float)p[i * 2], (float)p[i * 2 + 1]));
  }

  SkPath path = SkPath::Polygon(SkSpan<const SkPoint>(pts.data(), n), true);

  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(style->fill_colour));
  paint.setStyle(SkPaint::kFill_Style);
  active_canvas->drawPath(path, paint);
  return NSERROR_OK;
}

nserror PlotPath(const struct redraw_context* ctx, const plot_style_t* pstyle,
                 const float* p, unsigned int n, const float transform[6]) {
  if (!active_canvas || !p) return NSERROR_INVALID;
  if (n == 0) return NSERROR_OK;
  if (static_cast<int>(p[0]) != PLOTTER_PATH_MOVE) return NSERROR_INVALID;

  auto tx = [&](float x, float y) {
    return transform[0] * x + transform[2] * y + transform[4];
  };
  auto ty = [&](float x, float y) {
    return transform[1] * x + transform[3] * y + transform[5];
  };

  SkPathBuilder builder;
  builder.setFillType(SkPathFillType::kWinding);
  bool empty_path = true;
  for (unsigned int i = 0; i < n;) {
    int cmd = static_cast<int>(p[i]);
    if (cmd == PLOTTER_PATH_MOVE) {
      if (i + 2 >= n) return NSERROR_INVALID;
      builder.moveTo(tx(p[i + 1], p[i + 2]), ty(p[i + 1], p[i + 2]));
      i += 3;
    } else if (cmd == PLOTTER_PATH_CLOSE) {
      if (!empty_path) builder.close();
      i++;
    } else if (cmd == PLOTTER_PATH_LINE) {
      if (i + 2 >= n) return NSERROR_INVALID;
      builder.lineTo(tx(p[i + 1], p[i + 2]), ty(p[i + 1], p[i + 2]));
      i += 3;
      empty_path = false;
    } else if (cmd == PLOTTER_PATH_BEZIER) {
      if (i + 6 >= n) return NSERROR_INVALID;
      builder.cubicTo(tx(p[i + 1], p[i + 2]), ty(p[i + 1], p[i + 2]),
                      tx(p[i + 3], p[i + 4]), ty(p[i + 3], p[i + 4]),
                      tx(p[i + 5], p[i + 6]), ty(p[i + 5], p[i + 6]));
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
    paint.setAntiAlias(true);
    paint.setColor(ConvertColor(pstyle->stroke_colour));
    paint.setStrokeWidth(plot_style_fixed_to_float(pstyle->stroke_width));
    paint.setStyle(SkPaint::kStroke_Style);
    active_canvas->drawPath(path, paint);
  }

  return NSERROR_OK;
}

nserror PlotText(const struct redraw_context* ctx,
                 const struct plot_font_style* fstyle, int x, int y,
                 const char* text, size_t length) {
  if (!active_canvas) return NSERROR_INVALID;
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) return NSERROR_INVALID;
  SkPaint paint;
  paint.setAntiAlias(true);
  paint.setColor(ConvertColor(fstyle->foreground));
  active_canvas->drawString(SkString(text, length), (float)x, (float)y, *font,
                            paint);
  return NSERROR_OK;
}

nserror PlotBitmap(const struct redraw_context* ctx, struct bitmap* bitmap,
                   int x, int y, int width, int height, colour bg,
                   bitmap_flags_t flags) {
  if (!active_canvas || !bitmap) return NSERROR_INVALID;
  if (width <= 0 || height <= 0) return NSERROR_OK;

  if (!bitmap->cached_image) {
    bitmap->cached_image = SkImages::RasterFromBitmap(bitmap->sk_bitmap);
  }
  if (!bitmap->cached_image) return NSERROR_INVALID;

  SkRect clip = active_canvas->getLocalClipBounds();

  float start_x = (float)x;
  float end_x = (float)(x + width);
  if (flags & BITMAPF_REPEAT_X) {
    start_x = (float)x + std::floor((clip.left() - (float)x) / (float)width) *
                             (float)width;
    end_x = clip.right();
  }

  float start_y = (float)y;
  float end_y = (float)(y + height);
  if (flags & BITMAPF_REPEAT_Y) {
    start_y = (float)y + std::floor((clip.top() - (float)y) / (float)height) *
                             (float)height;
    end_y = clip.bottom();
  }

  SkColor bg_color = ConvertColor(bg);
  bool has_bg = (SkColorGetA(bg_color) > 0);
  SkPaint bg_paint;
  if (has_bg) {
    bg_paint.setColor(bg_color);
    bg_paint.setStyle(SkPaint::kFill_Style);
  }

  for (float cy = start_y; cy < end_y; cy += (float)height) {
    for (float cx = start_x; cx < end_x; cx += (float)width) {
      SkRect dest = SkRect::MakeXYWH(cx, cy, (float)width, (float)height);
      if (has_bg) active_canvas->drawRect(dest, bg_paint);
      active_canvas->drawImageRect(bitmap->cached_image, dest,
                                   SkSamplingOptions());
    }
  }
  return NSERROR_OK;
}

}  // namespace

void SetActiveCanvas(SkCanvas* canvas) { active_canvas = canvas; }
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
    .option_knockout = true,
};

}  // namespace perception
}  // namespace netsurf
