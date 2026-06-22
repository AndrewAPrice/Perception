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

#include "include/core/SkBitmap.h"
#include "include/core/SkFont.h"
#include "include/core/SkImage.h"

struct plot_font_style;

// Global-namespace structure for Skia bitmap mappings
struct bitmap {
	SkBitmap sk_bitmap;
	sk_sp<SkImage> cached_image;
};

extern "C" {
#include "utils/errors.h"
#include "netsurf/content_type.h"
#include "netsurf/browser_window.h"
#include "netsurf/window.h"
#include "netsurf/misc.h"
#include "netsurf/clipboard.h"
#include "netsurf/fetch.h"
#include "netsurf/bitmap.h"
#include "netsurf/layout.h"
}

namespace netsurf {
namespace perception {

SkFont* GetSkiaFont(const struct plot_font_style *fstyle);

extern char **respaths;
extern char *fename;
extern int fewidth;
extern int feheight;
extern int febpp;
extern char *feurl;

extern struct gui_misc_table perception_misc_table;
extern struct gui_window_table perception_window_table;
extern struct gui_clipboard_table perception_clipboard_table;
extern struct gui_fetch_table perception_fetch_table;
extern struct gui_bitmap_table skia_bitmap_table;
extern struct gui_layout_table skia_layout_table;

char **FbInitResourcePath(const char *resource_path);

}  // namespace perception
}  // namespace netsurf

// Keep global symbols in C context so NetSurf core can see them
using ::netsurf::perception::respaths;
using ::netsurf::perception::fename;
using ::netsurf::perception::fewidth;
using ::netsurf::perception::feheight;
using ::netsurf::perception::febpp;
using ::netsurf::perception::feurl;
using ::netsurf::perception::perception_misc_table;
using ::netsurf::perception::perception_window_table;
using ::netsurf::perception::perception_clipboard_table;
using ::netsurf::perception::perception_fetch_table;
using ::netsurf::perception::skia_bitmap_table;
using ::netsurf::perception::skia_layout_table;
