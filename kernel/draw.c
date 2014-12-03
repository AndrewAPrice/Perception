#include "draw.h"
void draw_sprite_1bit_alpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t start_x = x;
	size_t start_y = y;

	size_t end_x = x + width;
	size_t end_y = y + height;

	if(end_x >= buffer_width)
		end_x = buffer_width;

	if(end_y >= buffer_height)
		end_y = buffer_height;

	size_t out_indx = buffer_width * start_y + start_x;
	size_t in_indx = 0;
	size_t _x, _y;
	for(_y = start_y; _y != end_y; _y++) {
		size_t next_out_indx = out_indx + buffer_width;
		size_t next_in_indx = in_indx + width;
		for(_x = start_x; _x != end_x; _x++) {
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

void draw_sprite(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height, size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t start_x = x;
	size_t start_y = y;

	size_t end_x = x + width;
	size_t end_y = y + height;

	if(end_x >= buffer_width)
		end_x = buffer_width;

	if(end_y >= buffer_height)
		end_y = buffer_height;

	size_t out_indx = buffer_width * start_y + start_x;
	size_t in_indx = 0;
	size_t _x, _y;
	for(_y = start_y; _y != end_y; _y++) {
		size_t next_out_indx = out_indx + buffer_width;
		size_t next_in_indx = in_indx + width;
		for(_x = start_x; _x != end_x; _x++) {
			uint32 clr = sprite[in_indx];
			buffer[out_indx] = clr;
			out_indx++;
			in_indx++;
		}
		out_indx = next_out_indx;
		in_indx = next_in_indx;
	}
}

void draw_x_line(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(y >= buffer_height)
		return;

	size_t end_x = x + width;

	if(end_x >= buffer_width)
		end_x = buffer_width - 1;

	size_t indx = buffer_width * y + x;
	for(;x != end_x;x++, indx++)
		buffer[indx] = colour;
}

void draw_y_line(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(x >= buffer_width)
		return;

	size_t end_y = y + height;

	if(end_y >= buffer_height)
		end_y = buffer_height - 1;

	size_t indx = buffer_width * y + x;
	for(;y != end_y;y++, indx+=buffer_width)
		buffer[indx] = colour;
}

void plot_pixel(size_t x, size_t y, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	if(x >= buffer_width || y >= buffer_height)
		return;

	size_t indx = buffer_width * y + x;
	buffer[indx] = colour;
}

void fill_rectangle(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
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

void fill_rectangle_alpha(size_t minx, size_t miny, size_t maxx, size_t maxy, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	uint8 *colour_components = (uint8 *)&colour;
	size_t alpha = colour_components[3] + 1;
	size_t inv_alpha = 256 - colour_components[3];
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
			sc_buf[0] = (uint8)((alpha * colour_components[0] + inv_alpha * sc_buf[0]) >> 8);
			sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
			sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);

			indx++;
		}
		indx = next_indx;
	}
}
