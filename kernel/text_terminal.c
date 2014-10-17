#include "text_terminal.h"
#include "io.h"
#include "virtual_allocator.h"

const size_t screen_width = 80;
const size_t screen_height = 25;
volatile char *text_video_memory = (char *)(0xB8000 + virtual_memory_offset);
unsigned short text_x_pos;
unsigned short text_y_pos;

/* enter text mode */
void enter_text_mode() {
	/* clear the screen as red with white text */
	size_t i;
	for(i = 0; i < screen_width * screen_height; i++) {
		text_video_memory[i*2] = ' ';
		((unsigned char *)text_video_memory)[i*2 + 1] = 0x4F;
	}

	text_x_pos = 0;
	text_y_pos = 0;
	update_text_cursor();
}

/* print a single character */
void print_char(char c) {
	if(c == '\b') { /* backspace */
		if(text_x_pos != 0)
			text_x_pos--;
	}
	else if(c == '\t') /* tab */
		text_x_pos = (text_x_pos + 8) & ~(8 - 1);
	else if(c == '\r') /* carriage return */
		text_x_pos = 0;
	else if(c == '\n') { /* newline */
		text_x_pos = 0;
		text_y_pos++;
	} else if(c >= ' ') { /* printable character */
		text_video_memory[(text_y_pos * screen_width + text_x_pos) * 2] = c;
		text_x_pos++;
	}

	text_mode_scroll();
	update_text_cursor();
}

/* print a null-terminated string */
void print_string(const char *str) {
	char *str1 = (char *)str;
	while(*str1) {
		print_char(*str1);
		str1++;
	}
}

/* print a fixed length string */
void print_fixed_string(const char *str, size_t length) {
	size_t i;
	for(i = 0; i < length; i++)
		print_char(str[i]);
}

/* print a number as a 64-bit hexidecimal string */
void print_hex(size_t h) {
	print_char('0');
	print_char('x');
	const char *charset = "0123456789ABCDEF";
	char temp[16];

	size_t i;
	for(i = 0; i < 16; i++) {
		temp[i] = charset[h % 16];
		h /= 16;
	}

	print_char(temp[15]);
	print_char(temp[14]);
	print_char(temp[13]);
	print_char(temp[12]);
	print_char('-');
	print_char(temp[11]);
	print_char(temp[10]);
	print_char(temp[9]);
	print_char(temp[8]);
	print_char('-');
	print_char(temp[7]);
	print_char(temp[6]);
	print_char(temp[5]);
	print_char(temp[4]);
	print_char('-');
	print_char(temp[3]);
	print_char(temp[2]);
	print_char(temp[1]);
	print_char(temp[0]);
}

/* print a number as a decimal string */
void print_number(size_t n) {
	if(n == 0) {
		print_char('0');
		return;
	}

	/* maximum value is 18,446,744,073,709,551,615
	                    01123445677891111111111111
	                                 0012334566789*/
	char temp[20];
	size_t first_char = 20;

	while(n > 0) {
		first_char--;
		temp[first_char] = '0' + (char)(n % 10);
		n /= 10;
	}

	size_t i;
	for(i = first_char; i < 20; i++) {
		print_char(temp[i]);
		if(i == 1 || i == 4 || i == 7 || i == 10 || i == 13 || i == 16)
			print_char(',');

	}
}

/* scrolls a line of text */
void text_mode_scroll() {
	if(text_x_pos >= screen_width) {
		text_x_pos = 0;
		text_y_pos++;
	}
	if(text_y_pos >= screen_height) {
		/* scroll all text up */
		size_t i;
		for(i = 0; i < screen_width * (screen_height - 1); i++) {
			text_video_memory[i*2] = text_video_memory[(i + screen_width)*2];
		}

		/* blank out the last line */
		for(i = 0; i < screen_width; i++) {
			text_video_memory[(i + (screen_width * (screen_height - 1))) * 2] = ' ';
		}

		text_y_pos = screen_height - 1;
		text_x_pos = 0;
	}
}

void update_text_cursor() {
	unsigned short cursor_pos = text_y_pos * screen_width + text_x_pos;

	outportb(0x3D4, 14);
	outportb(0x3D5, (unsigned char)(cursor_pos >> 8));
	outportb(0x3D4, 15);
	outportb(0x3D5, (unsigned char)cursor_pos);
}
