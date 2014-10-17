#pragma once
#include "types.h"

/* enter text mode */
extern void enter_text_mode();
/* print a single character */
extern void print_char(char c);
/* print a null-terminated string */
extern void print_string(const char *str);
/* print a fixed length string */
extern void print_fixed_string(const char *str, size_t length);
/* print a number as a 64-bit hexidecimal string */
extern void print_hex(size_t h);
/* print a number as a decimal string */
extern void print_number(size_t n);
/* scrolls a line of text */
extern void text_mode_scroll();
/* updates the hardware cursor */
extern void update_text_cursor();
