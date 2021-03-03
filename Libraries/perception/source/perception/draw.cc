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

namespace perception {

void DrawSprite1bitAlpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t start_x = x;
	size_t start_y = y;

	if(minx > start_x)
		start_x = minx;

	if(miny > start_y)
		start_y = miny;

	size_t end_x = x + width;
	size_t end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	size_t out_indx = buffer_width * start_y + start_x;
	size_t in_indx = (start_x - x) + width * (start_y - y);
	size_t _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		size_t next_out_indx = out_indx + buffer_width;
		size_t next_in_indx = in_indx + width;
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

void DrawSpriteAlpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t start_x = x;
	size_t start_y = y;

	if(minx > start_x)
		start_x = minx;

	if(miny > start_y)
		start_y = miny;

	size_t end_x = x + width;
	size_t end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	size_t out_indx = buffer_width * start_y + start_x;
	size_t in_indx = (start_x - x) + width * (start_y - y);
	size_t _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		size_t next_out_indx = out_indx + buffer_width;
		size_t next_in_indx = in_indx + width;
		for(_x = start_x; _x < end_x; _x++) {
			uint8 *colour_components = (uint8 *)(&sprite[in_indx]);
			size_t alpha = colour_components[0] + 1;
			size_t inv_alpha = 256 - colour_components[0];
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

void DrawSprite(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t start_x = x;
	size_t start_y = y;

	if(start_x < minx)
		start_x = minx;

	if(start_y < miny)
		start_y = miny;

	size_t end_x = x + width;
	size_t end_y = y + height;

	if(end_x > maxx)
		end_x = maxx;

	if(end_y > maxy)
		end_y = maxy;

	size_t out_indx = buffer_width * start_y + start_x;
	size_t in_indx = (start_x - x) + width * (start_y - y);

	size_t _x, _y;

	for(_y = start_y; _y < end_y; _y++) {
		size_t next_out_indx = out_indx + buffer_width;
		size_t next_in_indx = in_indx + width;

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

void DrawXLine(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(y >= buffer_height)
		return;

	size_t end_x = x + width;

	if(end_x >= buffer_width)
		end_x = buffer_width;

	size_t indx = buffer_width * y + x;
	for(;x != end_x;x++, indx++)
		buffer[indx] = colour;
}

void DrawXLineAlpha(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(y >= buffer_height)
		return;

	size_t end_x = x + width;

	if(end_x >= buffer_width)
		end_x = buffer_width;

	uint8 *colour_components = (uint8 *)&colour;
	size_t alpha = colour_components[0] + 1;
	size_t inv_alpha = 256 - colour_components[0];

	size_t indx = buffer_width * y + x;
	for(;x != end_x;x++, indx++) {
		uint8 *sc_buf = (uint8 *)(&buffer[indx]);
		sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
		sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
		sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);
	}
}

void DrawYLine(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(x >= buffer_width)
		return;

	size_t end_y = y + height;

	if(end_y >= buffer_height)
		end_y = buffer_height;

	size_t indx = buffer_width * y + x;
	for(;y != end_y;y++, indx+=buffer_width)
		buffer[indx] = colour;
}

void DrawYLineAlpha(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(x >= buffer_width)
		return;

	size_t end_y = y + height;

	if(end_y >= buffer_height)
		end_y = buffer_height;

	uint8 *colour_components = (uint8 *)&colour;
	size_t alpha = colour_components[0] + 1;
	size_t inv_alpha = 256 - colour_components[0];

	size_t indx = buffer_width * y + x;
	for(;y != end_y;y++, indx+=buffer_width) {
		uint8 *sc_buf = (uint8 *)(&buffer[indx]);
		sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
		sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
		sc_buf[3] = (uint8)((alpha * colour_components[3] + inv_alpha * sc_buf[3]) >> 8);
	}
}

void PlotPixel(size_t x, size_t y, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(x >= buffer_width || y >= buffer_height)
		return;

	size_t indx = buffer_width * y + x;
	buffer[indx] = colour;
}

void FillRectangle(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {

	if(maxx >= buffer_width)
		maxx = buffer_width;

	if(maxy >= buffer_height)
		maxy = buffer_height;

	size_t indx = buffer_width * miny + minx;
	size_t _x, _y;
	for(_y = miny; _y < maxy; _y++) {
		size_t next_indx = indx + buffer_width;
		for(_x = minx; _x < maxx; _x++) {
			buffer[indx] = colour;
			indx++;
		}
		indx = next_indx;
	}
}

void FillRectangleAlpha(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	uint8 *colour_components = (uint8 *)&colour;
	size_t alpha = colour_components[0] + 1;
	size_t inv_alpha = 256 - colour_components[0];
	if(maxx >= buffer_width)
		maxx = buffer_width;

	if(maxy >= buffer_height)
		maxy = buffer_height;

	size_t indx = buffer_width * miny + minx;
	size_t _x, _y;

	for(_y = miny; _y < maxy; _y++) {
		size_t next_indx = indx + buffer_width;
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
