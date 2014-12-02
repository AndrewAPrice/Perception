#pragma once
#include "types.h"

/* draws a sprite onto the screen with alpha */
extern void draw_sprite_1bit_alpha(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void draw_sprite(size_t x, size_t y, uint32 *sprite, size_t width, size_t height,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void draw_x_line(size_t x, size_t y, size_t width, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void draw_y_line(size_t x, size_t y, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void plot_pixel(size_t x, size_t y, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void fill_rectangle(size_t x, size_t y, size_t width, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern void fill_rectangle_alpha(size_t x, size_t y, size_t width, size_t height, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
