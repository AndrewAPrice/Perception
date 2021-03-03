// Copyright 2021 Google LLC
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

#include "types.h"

namespace perception {

void DrawSprite1bitAlpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy);
void DrawSpriteAlpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy);
void DrawSprite(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy);
void DrawXLine(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void DrawXLineAlpha(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void DrawYLine(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void DrawYLineAlpha(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void PlotPixel(size_t x, size_t y, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void FillRectangle(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
void FillRectangleAlpha(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
	
}
