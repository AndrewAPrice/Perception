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

void DrawSprite1bitAlpha(int x, int y, uint32 *sprite, int width, int height,
	uint32 *buffer, int buffer_width, int buffer_height, int minx, int miny,
	int maxx, int maxy);
void DrawSpriteAlpha(int x, int y, uint32 *sprite, int width,
	int height, uint32 *buffer, int buffer_width, int buffer_height,
	int minx, int miny, int maxx, int maxy);
void DrawSprite(int x, int y, uint32 *sprite, int width, int height,
	uint32 *buffer, int buffer_width, int buffer_height, int minx,
	int miny, int maxx, int maxy);
void DrawXLine(int x, int y, int width, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height);
void DrawXLineAlpha(int x, int y, int width, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height);
void DrawYLine(int x, int y, int height, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height);
void DrawYLineAlpha(int x, int y, int height, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height);
void PlotPixel(int x, int y, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height);
void FillRectangle(int minx, int miny, int maxx, int maxy,
	uint32 colour, uint32 *buffer, int buffer_width, int buffer_height);
void FillRectangleAlpha(int minx, int miny, int maxx, int maxy,
	uint32 colour, uint32 *buffer, int buffer_width, int buffer_height);
	
}
