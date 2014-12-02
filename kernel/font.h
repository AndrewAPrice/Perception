#pragma once
#include "types.h"

#define FONT_HEIGHT 8

extern void init_font();
extern void draw_string(uint16 x, uint16 y, char *str, size_t str_len, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height);
extern size_t measure_string(char *str, size_t str_len);
