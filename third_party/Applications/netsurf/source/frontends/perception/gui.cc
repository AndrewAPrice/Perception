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

#include "gui.h"

#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <vector>

#include "plotters.h"
#include "settings.h"
#include "window.h"

extern "C" {
#include <libwapcaplet/libwapcaplet.h>

#include "content/fetch.h"
#include "content/fetchers.h"
#include "desktop/browser_history.h"
#include "netsurf/bitmap.h"
#include "netsurf/browser_window.h"
#include "netsurf/clipboard.h"
#include "netsurf/content_type.h"
#include "netsurf/cookie_db.h"
#include "netsurf/fetch.h"
#include "netsurf/layout.h"
#include "netsurf/misc.h"
#include "netsurf/mouse.h"
#include "netsurf/netsurf.h"
#include "netsurf/plot_style.h"
#include "netsurf/plotters.h"
#include "netsurf/window.h"
#include "utils/file.h"
#include "utils/filepath.h"
#include "utils/log.h"
#include "utils/messages.h"
#include "utils/nsoption.h"
#include "utils/nsurl.h"
content_status content_get_status(struct hlcache_handle* h);
const uint8_t* content_get_source_data(struct hlcache_handle* h, size_t* size);
}

#include <sys/time.h>
#include <time.h>

#include <iostream>

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkRect.h"
#include "include/core/SkString.h"
#include "perception/debug.h"
#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/input_box.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/scroll_bar.h"
#include "perception/ui/components/tab_bar.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/draw_context.h"
#include "perception/ui/font.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"
#include "perception/ui/theme.h"

using ::perception::HandOverControl;
using ::perception::TerminateProcess;
using ::perception::ui::DrawContext;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::Point;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::InputBox;
using ::perception::ui::components::Label;
using ::perception::ui::components::ScrollBar;
using ::perception::ui::components::TabBar;
using ::perception::ui::components::UiWindow;

namespace netsurf {
namespace perception {

std::recursive_mutex netsurf_mutex;

void gui_quit();

SkFont* GetSkiaFont(const struct plot_font_style* fstyle) {
  std::string family_name;
  if (fstyle->families) {
    for (int i = 0; fstyle->families[i] != nullptr; ++i) {
      std::string name(lwc_string_data(fstyle->families[i]),
                       lwc_string_length(fstyle->families[i]));
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (name.find("dejavu") != std::string::npos ||
          name.find("sans") != std::string::npos ||
          name.find("serif") != std::string::npos ||
          name.find("mono") != std::string::npos ||
          name.find("math") != std::string::npos ||
          name.find("courier") != std::string::npos ||
          name.find("times") != std::string::npos ||
          name.find("arial") != std::string::npos ||
          name.find("helvetica") != std::string::npos ||
          name.find("georgia") != std::string::npos ||
          name.find("tahoma") != std::string::npos ||
          name.find("verdana") != std::string::npos) {
        family_name = std::string(lwc_string_data(fstyle->families[i]),
                                  lwc_string_length(fstyle->families[i]));
        break;
      }
    }
  }
  if (family_name.empty()) {
    switch (fstyle->family) {
      case PLOT_FONT_FAMILY_SERIF:
        family_name = "DejaVuSerif";
        break;
      case PLOT_FONT_FAMILY_MONOSPACE:
        family_name = "DejaVuSansMono";
        break;
      case PLOT_FONT_FAMILY_SANS_SERIF:
      default:
        family_name = "DejaVuSans";
        break;
    }
  }

  float size = plot_style_fixed_to_float(fstyle->size);
  if (size <= 0.0f) size = 12.0f;

  int weight = fstyle->weight;
  if (weight <= 0) weight = 400;

  bool is_italic = (fstyle->flags & (FONTF_ITALIC | FONTF_OBLIQUE)) != 0;

  return ::perception::ui::GetUiFont(
      family_name, size, weight,
      is_italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant,
      SkFontStyle::kNormal_Width);
}

namespace {

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

::perception::window::Cursor MapPointerShape(gui_pointer_shape shape) {
  switch (shape) {
    case GUI_POINTER_POINT:
      return ::perception::window::Cursor::Poke;
    case GUI_POINTER_CARET:
      return ::perception::window::Cursor::Caret;
    case GUI_POINTER_MOVE:
      return ::perception::window::Cursor::Drag;
    case GUI_POINTER_UP:
    case GUI_POINTER_DOWN:
      return ::perception::window::Cursor::ResizeVertical;
    case GUI_POINTER_LEFT:
    case GUI_POINTER_RIGHT:
      return ::perception::window::Cursor::ResizeHorizontal;
    case GUI_POINTER_RU:
    case GUI_POINTER_LD:
      return ::perception::window::Cursor::ResizeDiagonalTopRightBottomLeft;
    case GUI_POINTER_LU:
    case GUI_POINTER_RD:
      return ::perception::window::Cursor::ResizeDiagonalTopLeftBottomRight;
    default:
      return ::perception::window::Cursor::Pointer;
  }
}

/* Bitmap Table Functions */
void* BitmapCreate(int width, int height, enum gui_bitmap_flags flags) {
  struct bitmap* bm = new struct bitmap();
  SkColorType color_type = kRGBA_8888_SkColorType;
  SkAlphaType alpha_type =
      (flags & BITMAP_OPAQUE) ? kOpaque_SkAlphaType : kPremul_SkAlphaType;
  bm->sk_bitmap.allocPixels(
      SkImageInfo::Make(width, height, color_type, alpha_type));
  return bm;
}

void BitmapDestroy(void* bitmap) { delete (struct bitmap*)bitmap; }

unsigned char* BitmapGetBuffer(void* bitmap) {
  struct bitmap* bm = (struct bitmap*)bitmap;
  return (unsigned char*)bm->sk_bitmap.getPixels();
}

size_t BitmapGetRowstride(void* bitmap) {
  struct bitmap* bm = (struct bitmap*)bitmap;
  return bm->sk_bitmap.rowBytes();
}

int BitmapGetWidth(void* bitmap) {
  struct bitmap* bm = (struct bitmap*)bitmap;
  return bm->sk_bitmap.width();
}

int BitmapGetHeight(void* bitmap) {
  struct bitmap* bm = (struct bitmap*)bitmap;
  return bm->sk_bitmap.height();
}

void BitmapModified(void* bitmap) {
  struct bitmap* bm = (struct bitmap*)bitmap;
  bm->cached_image.reset();
}

nserror BitmapRender(struct bitmap* bitmap, struct hlcache_handle* content) {
  return NSERROR_OK;
}

/* Layout Table */
size_t GetNextUtf8CharLength(std::string_view s, size_t index) {
  if (index >= s.length()) return 0;
  unsigned char c = s[index];
  if ((c & 0x80) == 0) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 1;
}

nserror FontWidth(const struct plot_font_style* fstyle, const char* text,
                  size_t length, int* width) {
  if (!text || length == 0) {
    *width = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *width = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *width = 0;
    return NSERROR_OK;
  }
  SkRect bounds;
  font->measureText(text, safe_len, SkTextEncoding::kUTF8, &bounds);
  *width = (int)std::ceil(bounds.width());
  return NSERROR_OK;
}

nserror FontPosition(const struct plot_font_style* fstyle, const char* text,
                     size_t length, int x, size_t* char_offset, int* actual_x) {
  if (!text || length == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  float best_dist = std::abs((float)x);
  size_t best_idx = 0;
  float best_x = 0.0f;

  std::string_view sv(text, safe_len);
  size_t idx = 0;
  while (true) {
    SkRect bounds;
    font->measureText(text, idx, SkTextEncoding::kUTF8, &bounds);
    float w = bounds.width();
    float dist = std::abs(w - x);
    if (dist < best_dist) {
      best_dist = dist;
      best_idx = idx;
      best_x = w;
    } else {
      break;
    }
    if (idx >= safe_len) break;
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  *char_offset = best_idx;
  *actual_x = (int)best_x;
  return NSERROR_OK;
}

nserror FontSplit(const struct plot_font_style* fstyle, const char* text,
                  size_t length, int x, size_t* char_offset, int* actual_x) {
  if (!text || length == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t safe_len = strnlen(text, length);
  if (safe_len == 0) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  SkFont* font = GetSkiaFont(fstyle);
  if (!font) {
    *char_offset = 0;
    *actual_x = 0;
    return NSERROR_OK;
  }
  size_t best_idx = 0;
  float best_x = 0.0f;

  std::string_view sv(text, safe_len);
  size_t idx = 0;
  while (true) {
    SkRect bounds;
    font->measureText(text, idx, SkTextEncoding::kUTF8, &bounds);
    float w = bounds.width();
    if (w <= x) {
      best_idx = idx;
      best_x = w;
    } else {
      break;
    }
    if (idx >= safe_len) break;
    size_t char_len = GetNextUtf8CharLength(sv, idx);
    if (char_len == 0) break;
    idx += char_len;
  }
  *char_offset = best_idx;
  *actual_x = (int)best_x;
  return NSERROR_OK;
}

/* Fetch Table */
const char* FileType(const char* unix_path) {
  std::string_view path(unix_path);
  size_t dot = path.find_last_of('.');
  if (dot != std::string_view::npos) {
    std::string_view ext = path.substr(dot + 1);
    if (ext == "html" || ext == "htm") return "text/html";
    if (ext == "css") return "text/css";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "gif") return "image/gif";
    if (ext == "txt") return "text/plain";
  }
  return "application/octet-stream";
}

struct nsurl* GetResourceUrl(const char* path) {
  char buf[PATH_MAX];
  struct nsurl* url = nullptr;

  if (strcmp(path, "favicon.ico") == 0) {
    path = "favicon.png";
  }

  char* found_path = filepath_sfind(respaths, buf, path);
  if (found_path != nullptr) {
    netsurf_path_to_nsurl(found_path, &url);
  } else {
    std::string fallback_path =
        "file:///Applications/netsurf/res/" + std::string(path);
    nsurl_create(fallback_path.c_str(), &url);
  }
  return url;
}

struct ScheduledTimer {
  void (*callback)(void* p);
  void* p;
  bool cancelled;
};

std::vector<std::shared_ptr<ScheduledTimer>> active_timers;

nserror FramebufferSchedule(int tival, void (*callback)(void* p), void* p) {
  /* Unschedule/cancel any existing timer matching callback and p */
  for (auto& timer : active_timers) {
    if (timer->callback == callback && timer->p == p) {
      timer->cancelled = true;
    }
  }

  /* Clean up cancelled/inactive timers */
  active_timers.erase(
      std::remove_if(active_timers.begin(), active_timers.end(),
                     [](const std::shared_ptr<ScheduledTimer>& t) {
                       return t->cancelled;
                     }),
      active_timers.end());

  /* If tival is negative, it is an unschedule request, so just return! */
  if (tival < 0) {
    return NSERROR_OK;
  }

  auto timer = std::make_shared<ScheduledTimer>();
  timer->callback = callback;
  timer->p = p;
  timer->cancelled = false;
  active_timers.push_back(timer);

  if (tival == 0) {
    ::perception::Defer([timer]() {
      if (!timer->cancelled) {
        NETSURF_LOCK;
        timer->callback(timer->p);
      }
    });
  } else {
    ::perception::Defer([tival, timer]() {
      ::perception::SleepForDuration(std::chrono::milliseconds(tival));
      if (!timer->cancelled) {
        NETSURF_LOCK;
        timer->callback(timer->p);
      }
    });
  }
  return NSERROR_OK;
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* existing,
                                            gui_window_create_flags flags);

static void gui_window_destroy(struct gui_window* gw);

nserror FbWindowInvalidateArea(struct gui_window* g, const struct rect* rect) {
  if (g && g->content_node) {
    g->content_node->Invalidate();
  }
  return NSERROR_OK;
}

}  // namespace

void UpdateScrollBars(struct gui_window* gw) {
  if (!gw || !gw->content_node) return;

  int content_width = 0;
  int content_height = 0;
  if (gw->bw) {
    browser_window_get_extents(gw->bw, false, &content_width, &content_height);
  }

  if (content_width == 0) content_width = 800;
  if (content_height == 0) content_height = 600;

  float viewport_width = gw->content_node->GetSize().width;
  float viewport_height = gw->content_node->GetSize().height;

  if (viewport_width < 1.0f) viewport_width = 1.0f;
  if (viewport_height < 1.0f) viewport_height = 1.0f;

  if (gw->horizontal_scroll_bar) {
    gw->horizontal_scroll_bar->SetValue(0.0f, (float)content_width,
                                        (float)gw->scroll.x, viewport_width);
  }
  if (gw->vertical_scroll_bar) {
    gw->vertical_scroll_bar->SetValue(0.0f, (float)content_height,
                                      (float)gw->scroll.y, viewport_height);
  }
}

namespace {

bool GuiWindowGetScroll(struct gui_window* g, int* sx, int* sy) {
  NETSURF_LOCK;
  *sx = g ? (int)g->scroll.x : 0;
  *sy = g ? (int)g->scroll.y : 0;
  return true;
}

nserror GuiWindowSetScroll(struct gui_window* gw, const struct rect* rect) {
  NETSURF_LOCK;
  if (gw) {
    gw->scroll.x = (float)rect->x0;
    gw->scroll.y = (float)rect->y0;
    gw->pending_scroll = gw->scroll;
    if (gw->content_node) gw->content_node->Invalidate();
    UpdateScrollBars(gw);
  }
  return NSERROR_OK;
}

nserror GuiWindowGetDimensions(struct gui_window* gw, int* width, int* height) {
  NETSURF_LOCK;
  if (gw && gw->content_node) {
    *width = (int)gw->content_node->GetSize().width;
    *height = (int)gw->content_node->GetSize().height;
  } else {
    *width = fewidth;
    *height = feheight;
  }
  return NSERROR_OK;
}

nserror GuiWindowEvent(struct gui_window* gw, enum gui_window_event event) {
  NETSURF_LOCK;
  if (event == GW_EVENT_UPDATE_EXTENT || event == GW_EVENT_NEW_CONTENT) {
    if (gw && !gw->extent_deferred) {
      gw->extent_deferred = true;
      auto is_alive = gw->is_alive;
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        NETSURF_LOCK;
        gw->extent_deferred = false;
        UpdateScrollBars(gw);
        if (gw->content_node) {
          gw->content_node->Invalidate();
        }
      });
    }
  }
  return NSERROR_OK;
}

nserror GuiWindowSetUrl(struct gui_window* g, nsurl* url) {
  if (g) {
    const char* url_str = url ? nsurl_access(url) : "NULL";
    if (global_url_input && !global_url_input->HasFocus()) {
      global_url_input->SetText(url_str);
    }
  }
  return NSERROR_OK;
}

void GuiWindowSetStatus(struct gui_window* g, const char* text) {
  if (g && global_status_label) global_status_label->SetText(text);
}

void GuiWindowSetPointer(struct gui_window* g, gui_pointer_shape shape) {
  if (g && g->content_node) g->content_node->SetCursor(MapPointerShape(shape));
}

void GuiWindowPlaceCaret(struct gui_window* g, int x, int y, int height,
                         const struct rect* clip) {}

void GuiWindowSetTitle(struct gui_window* gw, const char* title) {
  if (gw) {
    gw->title = title ? title : "";
    if (!gw->title_deferred) {
      gw->title_deferred = true;
      auto is_alive = gw->is_alive;
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        NETSURF_LOCK;
        gw->title_deferred = false;
        UpdateTabBar();
        if (global_ui_window) {
          global_ui_window->Invalidate();
        }
      });
    }
  }
}

void FlushPendingMouseHover(struct gui_window* gw) {
  if (gw && gw->has_pending_hover) {
    browser_window_mouse_click(gw->bw, gw->pending_hover_state,
                               (int)gw->pending_hover.x,
                               (int)gw->pending_hover.y);
    gw->has_pending_hover = false;
  }
}

static struct gui_window* gui_window_create(struct browser_window* bw,
                                            struct gui_window* existing,
                                            gui_window_create_flags flags) {
  struct gui_window* gw = new gui_window();
  gw->bw = bw;
  gw->scroll = {.x = 0.0f, .y = 0.0f};
  gw->title = "New Tab";

  /* Create Content Viewport Node for this tab */
  gw->content_node = Node::Empty([](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
  });

  auto h_scroll_bar_node = ScrollBar::HorizontalScrollBar(
      &gw->horizontal_scroll_bar,
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });
  auto v_scroll_bar_node = ScrollBar::VerticalScrollBar(
      &gw->vertical_scroll_bar,
      [](Layout& layout) { layout.SetAlignSelf(YGAlignStretch); });

  gw->tab_root_node = Container::HorizontalContainer(
      [](Layout& layout) {
        layout.SetFlexGrow(1.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetGap(0.0f);
      },
      Container::VerticalContainer(
          [](Layout& layout) {
            layout.SetFlexGrow(1.0f);
            layout.SetFlexShrink(1.0f);
            layout.SetAlignSelf(YGAlignStretch);
            layout.SetGap(0.0f);
          },
          gw->content_node, h_scroll_bar_node),
      v_scroll_bar_node);

  gw->horizontal_scroll_bar->OnScroll([gw](float value) {
    if (value == gw->pending_scroll.x) return;
    gw->pending_scroll.x = value;
    gw->has_pending_scroll = true;

    if (!gw->scroll_deferred) {
      gw->scroll_deferred = true;
      auto is_alive = gw->is_alive;
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->has_pending_scroll) {
          gw->scroll = gw->pending_scroll;
          gw->content_node->Invalidate();
          gw->has_pending_scroll = false;
        }
        gw->scroll_deferred = false;
      });
    }
  });

  gw->vertical_scroll_bar->OnScroll([gw](float value) {
    if (value == gw->pending_scroll.y) return;
    gw->pending_scroll.y = value;
    gw->has_pending_scroll = true;

    if (!gw->scroll_deferred) {
      gw->scroll_deferred = true;
      auto is_alive = gw->is_alive;
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->has_pending_scroll) {
          gw->scroll = gw->pending_scroll;
          gw->content_node->Invalidate();
          gw->has_pending_scroll = false;
        }
        gw->scroll_deferred = false;
      });
    }
  });

  /* Wire up Draw callback using Skia Canvas */
  gw->content_node->OnDraw([gw](const DrawContext& context) {
    NETSURF_LOCK;
    if (!context.skia_canvas) return;

    if (!browser_window_has_content(gw->bw)) {
      SkPaint paint;
      paint.setColor(SkColorSetARGB(255, 255, 255, 255));
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                           context.area.size.width, context.area.size.height),
          paint);
      return;
    }

    struct hlcache_handle* content = browser_window_get_content(gw->bw);
    if (!content) return;

    content_status status = content_get_status(content);
    if (status != CONTENT_STATUS_READY && status != CONTENT_STATUS_DONE) {
      SkPaint paint;
      paint.setColor(SkColorSetARGB(255, 255, 255, 255));
      context.skia_canvas->drawRect(
          SkRect::MakeXYWH(context.area.origin.x, context.area.origin.y,
                           context.area.size.width, context.area.size.height),
          paint);
      return;
    }

    int w = (int)context.area.size.width;
    int h = (int)context.area.size.height;

    if (w < 100) w = 100;
    if (h < 100) h = 100;

    active_canvas = context.skia_canvas;

    struct redraw_context ctx = {
        .interactive = true, .background_images = true, .plot = &skia_plotters};

    struct rect rect = {.x0 = (int)gw->scroll.x,
                        .y0 = (int)gw->scroll.y,
                        .x1 = (int)gw->scroll.x + w,
                        .y1 = (int)gw->scroll.y + h};

    active_canvas->save();
    active_canvas->translate(context.area.origin.x - gw->scroll.x,
                             context.area.origin.y - gw->scroll.y);
    active_canvas->save();

    browser_window_redraw(gw->bw, 0, 0, &rect, &ctx);

    active_canvas->restore();
    active_canvas->restore();
    active_canvas = nullptr;
  });

  /* Wire up Mouse interaction */
  gw->content_node->OnMouseHover([gw](const Point& p) {
    gw->pending_hover = {.x = p.x + gw->scroll.x, .y = p.y + gw->scroll.y};
    gw->pending_hover_state = BROWSER_MOUSE_HOVER;
    gw->has_pending_hover = true;

    if (!gw->hover_deferred) {
      gw->hover_deferred = true;
      auto is_alive = gw->is_alive;
      ::perception::DeferAfterEvents([gw, is_alive]() {
        if (!*is_alive) return;
        if (gw->has_pending_hover) {
          NETSURF_LOCK;
          browser_window_mouse_click(gw->bw, gw->pending_hover_state,
                                     (int)gw->pending_hover.x,
                                     (int)gw->pending_hover.y);
          gw->has_pending_hover = false;
        }
        gw->hover_deferred = false;
      });
    }
  });

  gw->content_node->OnMouseButtonDown(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        browser_mouse_state mouse_state = (browser_mouse_state)0;
        if (button == ::perception::window::MouseButton::Left) {
          mouse_state = BROWSER_MOUSE_PRESS_1;
        } else if (button == ::perception::window::MouseButton::Right) {
          mouse_state = BROWSER_MOUSE_PRESS_2;
        } else if (button == ::perception::window::MouseButton::Middle) {
          mouse_state = BROWSER_MOUSE_PRESS_3;
        }
        browser_window_mouse_click(gw->bw, mouse_state,
                                   (int)p.x + (int)gw->scroll.x,
                                   (int)p.y + (int)gw->scroll.y);
      });

  gw->content_node->OnMouseButtonUp(
      [gw](const Point& p, ::perception::window::MouseButton button) {
        NETSURF_LOCK;
        FlushPendingMouseHover(gw);
        browser_mouse_state mouse_state = (browser_mouse_state)0;
        if (button == ::perception::window::MouseButton::Left) {
          mouse_state = BROWSER_MOUSE_CLICK_1;
        } else if (button == ::perception::window::MouseButton::Right) {
          mouse_state = BROWSER_MOUSE_CLICK_2;
        } else if (button == ::perception::window::MouseButton::Middle) {
          mouse_state = BROWSER_MOUSE_CLICK_3;
        }
        browser_window_mouse_click(gw->bw, mouse_state,
                                   (int)p.x + (int)gw->scroll.x,
                                   (int)p.y + (int)gw->scroll.y);
      });

  open_tabs.push_back(gw);

  if (!global_ui_window) {
    /* Construct UI elements for the very first time */
    auto go_back = []() {
      NETSURF_LOCK;
      if (active_tab_index < open_tabs.size()) {
        browser_window_history_back(open_tabs[active_tab_index]->bw, false);
      }
    };

    auto go_forward = []() {
      NETSURF_LOCK;
      if (active_tab_index < open_tabs.size()) {
        browser_window_history_forward(open_tabs[active_tab_index]->bw, false);
      }
    };

    auto reload_page = []() {
      NETSURF_LOCK;
      if (active_tab_index < open_tabs.size()) {
        browser_window_reload(open_tabs[active_tab_index]->bw, false);
      }
    };

    viewport_container->AddChild(gw->tab_root_node);

    global_ui_window = UiWindow::ResizableWindowWithTabBar(
        &global_tab_bar,
        [](UiWindow& win) {
          win.OnClose([]() { gui_quit(); });
          win.OnResize([]() {
            NETSURF_LOCK;
            for (auto gw : open_tabs) {
              if (gw && gw->bw) {
                browser_window_schedule_reformat(gw->bw);
                if (gw->content_node) {
                  gw->content_node->Invalidate();
                }
              }
            }
          });
        },
        [](Layout& layout) {
          layout.SetWidth(640.0f);
          layout.SetHeight(480.0f);
          layout.SetPadding(YGEdgeHorizontal, 0.0f);
          layout.SetPadding(YGEdgeBottom, 0.0f);
        },
        Container::VerticalContainer(
            [](Layout& layout) {
              layout.SetFlexGrow(1.0f);
              layout.SetAlignSelf(YGAlignStretch);
              layout.SetGap(0.0f);
            },
            Container::HorizontalContainer(
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetHeight(40.0f);
                  layout.SetGap(8.0f);
                  layout.SetPadding(YGEdgeAll, 4.0f);
                  layout.SetMargin(YGEdgeHorizontal, 8.0f);
                },
                Button::TextButton("Back", go_back),
                Button::TextButton("Forward", go_forward),
                Button::TextButton("Reload", reload_page),
                InputBox::BasicInputBox(
                    "", [](Layout& layout) { layout.SetFlexGrow(1.0f); },
                    [](InputBox& input_box) {
                      input_box.OnEnterPressed([](std::string_view text) {
                        ::perception::DebugPrinterSingleton
                            << "NetSurf Native: OnEnterPressed for URL: \""
                            << std::string(text).c_str() << "\"\n";
                        NETSURF_LOCK;
                        if (active_tab_index < open_tabs.size()) {
                          std::string url_str(text);
                          if (url_str.find("://") == std::string::npos) {
                            url_str = "http://" + url_str;
                          }
                          nsurl* url;
                          nserror ret = nsurl_create(url_str.c_str(), &url);
                          ::perception::DebugPrinterSingleton
                              << "NetSurf Native: nsurl_create returned: "
                              << (int64)ret << "\n";
                          if (ret == NSERROR_OK) {
                            ::perception::DebugPrinterSingleton
                                << "NetSurf Native: Navigating active tab to "
                                   "URL!\n";
                            browser_window_navigate(
                                open_tabs[active_tab_index]->bw, url, NULL,
                                BW_NAVIGATE_NONE, NULL, NULL, NULL);
                            nsurl_unref(url);
                          }
                        }
                      });
                    },
                    &global_url_input)),
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            viewport_container,
            Block::SolidColor(::perception::ui::kContainerBorderColor,
                              [](Layout& layout) {
                                layout.SetAlignSelf(YGAlignStretch);
                                layout.SetHeight(1.0f);
                              }),
            Label::BasicLabel(
                "",
                [](Layout& layout) {
                  layout.SetAlignSelf(YGAlignStretch);
                  layout.SetHeight(20.0f);
                  layout.SetMargin(YGEdgeHorizontal, 8.0f);
                },
                [](Label& lbl) {
                  lbl.SetTextAlignment(
                      ::perception::ui::TextAlignment::MiddleLeft);
                },
                &global_status_label)));

    if (!global_ui_window->GetChildren().empty()) {
      global_ui_window->GetChildren().front()->Apply(
          [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 0.0f); });
    }

    global_tab_bar->SetPrefixNode(Label::BasicLabel(
        "NetSurf", [](Label& lbl) { lbl.SetColor(0xFFFFFFFF); },
        [](Layout& layout) { layout.SetMargin(YGEdgeHorizontal, 8.0f); }));

    viewport_container = Container::VerticalContainer([](Layout& layout) {
      layout.SetFlexGrow(1.0f);
      layout.SetAlignSelf(YGAlignStretch);
    });
    global_tab_bar->SetSuffixNode(Node::Empty(
        [](Layout& layout) {
          layout.SetMinWidth(20.0f);
          layout.SetAlignSelf(YGAlignStretch);
          layout.SetFlexDirection(YGFlexDirectionRow);
          layout.SetJustifyContent(YGJustifyCenter);
          layout.SetAlignItems(YGAlignStretch);
        },
        [](Node& node) {
          node.SetCursor(::perception::window::Cursor::Poke);
          node.OnMouseButtonUp([](const Point&,
                                  ::perception::window::MouseButton button) {
            if (button != ::perception::window::MouseButton::Left) return;
            ::perception::DebugPrinterSingleton
                << "NetSurf Native: Clicked '+' button to add a new tab.\n";
            ::perception::DeferAfterEvents([]() {
              NETSURF_LOCK;
              if (active_tab_index < open_tabs.size()) {
                struct nsurl* url = nullptr;
                nserror ret = nsurl_create(
                    "file:///Applications/netsurf/res/en/welcome.html", &url);
                ::perception::DebugPrinterSingleton
                    << "  nsurl_create returned " << (int64)ret << "\n";
                if (ret == NSERROR_OK) {
                  struct browser_window* new_bw = nullptr;
                  nserror create_ret = browser_window_create(
                      (browser_window_create_flags)(BW_CREATE_TAB |
                                                    BW_CREATE_FOREGROUND |
                                                    BW_CREATE_HISTORY),
                      url, nullptr, open_tabs[active_tab_index]->bw, &new_bw);
                  ::perception::DebugPrinterSingleton
                      << "  browser_window_create returned "
                      << (int64)create_ret << "\n";
                  nsurl_unref(url);
                }
              } else {
                ::perception::DebugPrinterSingleton
                    << "  active_tab_index (" << active_tab_index
                    << ") is out of range (open_tabs.size = "
                    << open_tabs.size() << ")\n";
              }
            });
          });
        },
        Label::BasicLabel("+", [](Label& lbl) {
          lbl.SetColor(0xFFFFFFFF);
          lbl.SetTextAlignment(::perception::ui::TextAlignment::MiddleCenter);
        })));
    global_tab_bar->OnTabSelected([](int index) { SwitchTab(index); });
    global_tab_bar->OnTabClosed([](int index) { CloseTab(index); });

    active_tab_index = 0;
  } else {
    /* Dynamic new tab path */
    active_tab_index = open_tabs.size() - 1;
    viewport_container->RemoveChildren();
    viewport_container->AddChild(gw->tab_root_node);
  }

  UpdateTabBar();
  global_ui_window->Invalidate();

  return gw;
}

static void gui_window_destroy(struct gui_window* gw) {
  NETSURF_LOCK;
  auto it = std::find(open_tabs.begin(), open_tabs.end(), gw);
  if (it != open_tabs.end()) {
    size_t index = std::distance(open_tabs.begin(), it);
    open_tabs.erase(it);

    if (open_tabs.empty()) {
      gui_quit();
      return;
    }

    if (active_tab_index >= open_tabs.size()) {
      active_tab_index = open_tabs.size() - 1;
    } else if (active_tab_index == index) {
      active_tab_index = (index > 0) ? index - 1 : 0;
    }

    auto active_gw = open_tabs[active_tab_index];
    viewport_container->RemoveChildren();
    viewport_container->AddChild(active_gw->tab_root_node);

    if (global_url_input && active_gw->bw) {
      struct nsurl* url = nullptr;
      nserror ret = browser_window_get_url(active_gw->bw, true, &url);
      if (ret == NSERROR_OK && url) {
        global_url_input->SetText(nsurl_access(url));
      }
    }

    UpdateTabBar();
    if (global_ui_window) global_ui_window->Invalidate();
  }
  delete gw;
}

}  // namespace

char** respaths = nullptr;
char* fename = (char*)"perception";
int fewidth = 800;
int feheight = 600;
int febpp = 32;
char* feurl = (char*)"https://www.google.com";

struct gui_misc_table perception_misc_table = {
    .schedule = FramebufferSchedule,
    .quit = gui_quit,
};

struct gui_window_table perception_window_table = {
    .create = gui_window_create,
    .destroy = gui_window_destroy,
    .invalidate = FbWindowInvalidateArea,
    .get_scroll = GuiWindowGetScroll,
    .set_scroll = GuiWindowSetScroll,
    .get_dimensions = GuiWindowGetDimensions,
    .event = GuiWindowEvent,

    .set_title = GuiWindowSetTitle,
    .set_url = GuiWindowSetUrl,
    .set_status = GuiWindowSetStatus,
    .set_pointer = GuiWindowSetPointer,
    .place_caret = GuiWindowPlaceCaret,
};

struct gui_clipboard_table perception_clipboard_table = {
    .get =
        [](char** buffer, size_t* length) {
          *buffer = NULL;
          *length = 0;
        },
    .set = [](const char* buffer, size_t length, nsclipboard_styles styles[],
              int n_styles) {}};

struct gui_fetch_table perception_fetch_table = {
    .filetype = FileType,
    .get_resource_url = GetResourceUrl,
};

struct gui_bitmap_table skia_bitmap_table = {
    .create = BitmapCreate,
    .destroy = BitmapDestroy,
    .set_opaque = [](void*, bool) {},
    .get_opaque = [](void*) { return false; },
    .get_buffer = BitmapGetBuffer,
    .get_rowstride = BitmapGetRowstride,
    .get_width = BitmapGetWidth,
    .get_height = BitmapGetHeight,
    .modified = BitmapModified,
    .render = BitmapRender,
};

struct gui_layout_table skia_layout_table = {
    .width = FontWidth,
    .position = FontPosition,
    .split = FontSplit,
};

static const char* get_language_env(void) {
  const char* envstr;

  envstr = getenv("LANGUAGE");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LC_ALL");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LC_MESSAGES");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  envstr = getenv("LANG");
  if ((envstr != NULL) && (envstr[0] != 0)) {
    return envstr;
  }

  return "en";
}

static char** get_language_names(void) {
  char** langv;        /* output string vector of languages */
  int langc;           /* count of languages in vector */
  const char* envlang; /* colon separated list of languages from environment */
  int lstart = 0;      /* offset to start of current language */
  int lunder = 0;      /* offset to underscore in current language */
  int lend = 0;        /* offset to end of current language */
  char* nlang;

  langv = (char**)calloc(32 + 2, sizeof(char*));
  if (langv == NULL) {
    return NULL;
  }

  envlang = get_language_env();

  for (langc = 0; langc < 32; langc++) {
    /* work through envlang splitting on : */
    while ((envlang[lend] != 0) && (envlang[lend] != ':') &&
           (envlang[lend] != '.')) {
      if (envlang[lend] == '_') {
        lunder = lend;
      }
      lend++;
    }
    /* place language in string vector */
    nlang = (char*)malloc(lend - lstart + 1);
    memcpy(nlang, envlang + lstart, lend - lstart);
    nlang[lend - lstart] = 0;
    langv[langc] = nlang;

    /* add language without specialisation to vector */
    if (lunder != lstart) {
      nlang = (char*)malloc(lunder - lstart + 1);
      memcpy(nlang, envlang + lstart, lunder - lstart);
      nlang[lunder - lstart] = 0;
      langv[++langc] = nlang;
    }

    /* if we stopped at the dot, move to the colon delimiter */
    while ((envlang[lend] != 0) && (envlang[lend] != ':')) {
      lend++;
    }
    if (envlang[lend] == 0) {
      /* reached end of environment language list */
      break;
    }
    lend++;
    lstart = lunder = lend;
  }
  return langv;
}

char** FbInitResourcePath(const char* resource_path) {
  char* pathv[2];
  pathv[0] = (char*)"/Applications/netsurf/res";
  pathv[1] = NULL;

  char** langv = get_language_names();
  char** respath = filepath_generate(pathv, (const char* const*)langv);

  if (langv) {
    for (int i = 0; langv[i] != NULL; i++) {
      free(langv[i]);
    }
    free(langv);
  }

  return respath;
}

void gui_quit(void) {
  NSLOG(netsurf, INFO, "gui_quit");
  if (nsoptions && nsoption_charp(cookie_jar)) {
    urldb_save_cookies(nsoption_charp(cookie_jar));
  }
  TerminateProcess();
}
}  // namespace perception
}  // namespace netsurf
