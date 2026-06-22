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

#include "bitmap.h"

#include <stdbool.h>
#include <stddef.h>

extern "C" {
#include "utils/errors.h"
#include "netsurf/bitmap.h"
}

#include "include/core/SkImageInfo.h"

namespace netsurf {
namespace perception {
namespace {

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

}  // namespace

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

}  // namespace perception
}  // namespace netsurf
