#include "DejaVuSans.inl"
#include "font.h"
#include "liballoc.h"
#include "video.h"

unsigned char *font_bitmap;
stb_fontchar *font_chars;

void init_font() {
	font_bitmap = malloc(STB_FONT_DejaVuSans_BITMAP_HEIGHT * STB_FONT_DejaVuSans_BITMAP_WIDTH);
	font_chars = malloc(sizeof(stb_fontchar) * STB_FONT_DejaVuSans_NUM_CHARS);

	stb_font_DejaVuSans(font_chars, (unsigned char (*)[STB_FONT_DejaVuSans_BITMAP_WIDTH])
		font_bitmap, FONT_HEIGHT);
}

void draw_string(uint16 x, uint16 y, char *str, size_t str_len, uint32 colour,
	uint32 *buffer, size_t buffer_width, size_t buffer_height) {
	uint8 *colour_components = (uint8 *)&colour;
	while(str_len > 0) {
		if(*str >= STB_FONT_DejaVuSans_FIRST_CHAR) {
			/* draw this character */
			stb_fontchar *font_char = &font_chars[*str - STB_FONT_DejaVuSans_FIRST_CHAR];

			size_t in_x = font_char->s0;
			size_t in_y = font_char->t0;
			size_t end_in_x = font_char->s1;
			size_t end_in_y = font_char->t1;

			size_t out_x = x + font_char->x0;
			size_t out_y = y + font_char->y0;

			size_t in_indx = in_y * STB_FONT_DejaVuSans_BITMAP_WIDTH + in_x;
			size_t out_indx = out_y * screen_width + out_x;

			size_t y; for(y = in_y; y < end_in_y; y++) {
				size_t next_in_indx = in_indx + STB_FONT_DejaVuSans_BITMAP_WIDTH;
				size_t next_out_indx = out_indx + screen_width;

				size_t x; for(x = in_x; x < end_in_x; x++) {
					uint8 c = font_bitmap[in_indx];
					if(c) {
						//screen_buffer[out_indx] = colour;
						size_t alpha = c + 1;
						size_t inv_alpha = 256 - c;

						uint8 *sc_buf = (uint8 *)(&screen_buffer[out_indx]);
						sc_buf[0] = (uint8)((alpha * colour_components[0] + inv_alpha * sc_buf[0]) >> 8);
						sc_buf[1] = (uint8)((alpha * colour_components[1] + inv_alpha * sc_buf[1]) >> 8);
						sc_buf[2] = (uint8)((alpha * colour_components[2] + inv_alpha * sc_buf[2]) >> 8);
						sc_buf[3] = 0xFF;
						//screen_buffer[out_indx] = (c << 24) | (c << 16) | (c << 8) | c;
					}
					in_indx++;
					out_indx++;
				}

				in_indx = next_in_indx;
				out_indx = next_out_indx;
			}



			/* move to the next position */
			x += font_char->advance_int;
		}
		str++;
		str_len--;
	}

}

size_t measure_string(char *str, size_t str_len) {
	size_t length = 0;
	while(str_len > 0) {
		if(*str >= STB_FONT_DejaVuSans_FIRST_CHAR)
			length += font_chars[*str - STB_FONT_DejaVuSans_FIRST_CHAR].advance_int;
		str++;
		str_len--;
	}

	return length;

}