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

#include "perception/draw.h"

#include <algorithm>

namespace perception {

void DrawSprite1bitAlpha(int x, int y, uint32 *sprite, int width, int height,
	uint32 *buffer, int buffer_width, int buffer_height, int minx, int miny,
	int maxx, int maxy) {
	int start_x = x;
	int start_y = y;

	if(minx > start_x)
		start_x = minx;

	if(miny > start_y)
		start_y = miny;

	int end_x = x + width;
	int end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	int out_indx = buffer_width * start_y + start_x;
	int in_indx = (start_x - x) + width * (start_y - y);
	int _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		int next_out_indx = out_indx + buffer_width;
		int next_in_indx = in_indx + width;
		for(_x = start_x; _x < end_x; _x++) {
			uint32 clr = sprite[in_indx];
			if(clr) /* test for transparency */
				buffer[out_indx] = clr;
			out_indx++;
			in_indx++;
		}
		out_indx = next_out_indx;
		in_indx = next_in_indx;
	}
}

void DrawSpriteAlpha(int x, int y, uint32 *sprite, int width, int height,
	uint32 *buffer, int buffer_width, int buffer_height, int minx, int miny,
	int maxx, int maxy) {
	int start_x = x;
	int start_y = y;

	if(minx > start_x)
		start_x = minx;

	if(miny > start_y)
		start_y = miny;

	int end_x = x + width;
	int end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	int out_indx = buffer_width * start_y + start_x;
	int in_indx = (start_x - x) + width * (start_y - y);
	int _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		int next_out_indx = out_indx + buffer_width;
		int next_in_indx = in_indx + width;
		for(_x = start_x; _x < end_x; _x++) {
			uint8 *colour_components = (uint8 *)(&sprite[in_indx]);
			int alpha = colour_components[0] + 1;
			int inv_alpha = 256 - colour_components[0];
			uint8 *sc_buf = (uint8 *)(&buffer[out_indx]);
			sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
			sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
			sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);

			out_indx++;
			in_indx++;
		}
		out_indx = next_out_indx;
		in_indx = next_in_indx;
	}
}

void DrawSprite(int x, int y, uint32 *sprite, int width, int height,
	uint32 *buffer, int buffer_width, int buffer_height, int minx, int miny,
	int maxx, int maxy) {
	int start_x = x;
	int start_y = y;

	if(start_x < minx)
		start_x = minx;

	if(start_y < miny)
		start_y = miny;

	int end_x = x + width;
	int end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	int out_indx = buffer_width * start_y + start_x;
	int in_indx = (start_x - x) + width * (start_y - y);

	int _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		int next_out_indx = out_indx + buffer_width;
		int next_in_indx = in_indx + width;

		for(_x = start_x; _x < end_x; _x++) {
			uint32 clr = sprite[in_indx];
			buffer[out_indx] = clr;
			out_indx++;
			in_indx++;
		}
		out_indx = next_out_indx;
		in_indx = next_in_indx;
	}
}

void DrawXLine(int x, int y, int width, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	if(y < 0 || y >= buffer_height)
		return;

	int end_x = std::min(x + width, buffer_width);
	x = std::max(0, x);

	if(end_x >= buffer_width)
		end_x = buffer_width;

	int indx = buffer_width * y + x;
	for(;x < end_x;x++, indx++)
		buffer[indx] = colour;
}

void DrawXLineAlpha(int x, int y, int width, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	if(y < 0 || y >= buffer_height)
		return;

	int end_x = std::min(x + width, buffer_width);
	x = std::max(0, x);

	uint8 *colour_components = (uint8 *)&colour;
	int alpha = colour_components[0] + 1;
	int inv_alpha = 256 - colour_components[0];

	int indx = buffer_width * y + x;
	for(;x < end_x;x++, indx++) {
		uint8 *sc_buf = (uint8 *)(&buffer[indx]);
		sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
		sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
		sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);
	}
}

void DrawYLine(int x, int y, int height, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	if(x < 0 || x >= buffer_width)
		return;

	int end_y = std::min(y + height, buffer_height);
	y = std::max(0, y);

	int indx = buffer_width * y + x;
	for(;y < end_y;y++, indx+=buffer_width)
		buffer[indx] = colour;
}

void DrawYLineAlpha(int x, int y, int height, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	if(x < 0 || x >= buffer_width)
		return;

	int end_y = std::min(y + height, buffer_height);
	y = std::max(0, y);

	uint8 *colour_components = (uint8 *)&colour;
	int alpha = colour_components[0] + 1;
	int inv_alpha = 256 - colour_components[0];

	int indx = buffer_width * y + x;
	for(;y < end_y;y++, indx+=buffer_width) {
		uint8 *sc_buf = (uint8 *)(&buffer[indx]);
		sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
		sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
		sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);
	}
}

void PlotPixel(int x, int y, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	if(x < 0 || y < 0 || x >= buffer_width || y >= buffer_height)
		return;

	int indx = buffer_width * y + x;
	buffer[indx] = colour;
}

void FillRectangle(int minx, int miny, int maxx, int maxy, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	minx = std::max(0, minx);
	miny = std::max(0, miny);
	maxx = std::min(maxx, buffer_width);
	maxy = std::min(maxy, buffer_height);

	int indx = buffer_width * miny + minx;
	int _x, _y;
	for(_y = miny; _y < maxy; _y++) {
		int next_indx = indx + buffer_width;
		for(_x = minx; _x < maxx; _x++) {
			buffer[indx] = colour;
			indx++;
		}
		indx = next_indx;
	}
}

void FillRectangleAlpha(int minx, int miny, int maxx, int maxy, uint32 colour,
	uint32 *buffer, int buffer_width, int buffer_height) {
	uint8 *colour_components = (uint8 *)&colour;

	minx = std::max(0, minx);
	miny = std::max(0, miny);
	maxx = std::min(maxx, buffer_width);
	maxy = std::min(maxy, buffer_height);

	int alpha = colour_components[0] + 1;
	int inv_alpha = 256 - colour_components[0];

	int indx = buffer_width * miny + minx;
	int _x, _y;

	for(_y = miny; _y < maxy; _y++) {
		int next_indx = indx + buffer_width;
		for(_x = minx; _x < maxx; _x++) {
			uint8 *sc_buf = (uint8 *)(&buffer[indx]);
			sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
			sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
			sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);

			indx++;
		}
		indx = next_indx;
	}
}

}
