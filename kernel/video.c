#include "liballoc.h"
#include "mouse.h"
#include "pci.h"
#include "text_terminal.h"
#include "vesa.h"
#include "vga.h"
#include "video.h"

void (*return_to_text_mode)();
void (*flip_screen_buffer)();

size_t screen_width;
size_t screen_height;
uint32 *screen_buffer;
bool dither_screen;

bool graphics_initialized;

uint8 dithering_table[] = {
	0, 48, 12, 60, 3, 51, 15, 63,
	32, 16, 44, 28, 35, 19, 47, 31,
	8, 56, 4, 52, 11, 59, 7, 55,
	40, 24, 36, 20, 43, 27, 39, 23,
	2, 50, 14, 62, 1, 49, 13, 61,
	34, 18, 46, 30, 33, 17, 46, 29,
	10, 58, 6, 54, 9, 57, 5, 53,
	42, 26, 38, 22, 41, 25, 37, 21
	};

void init_video() {
	graphics_initialized = false;
	flip_screen_buffer = 0;
	screen_width = 0;
	screen_height = 0;

	screen_buffer = 0;
	dither_screen = false;
}

void init_video_device(struct PCIDevice *device) {
	/* try the best one first */
	init_vesa(device);

	/* fallback */
	if(!device->driver)
		init_vga(device);

	if(device->driver)
		graphics_initialized = true;
}

void check_for_video() {
	if(graphics_initialized)
		return;

	enter_text_mode();
	print_string("Unfortunately, no supported graphics device has been found.");
	asm("cli");
	while(true) asm("hlt");
}


void update_screen_buffer() {
	/* release the old screen buffer */
	if(screen_buffer)
		free(screen_buffer);

	screen_buffer = (uint32 *)malloc(screen_width * screen_height * 4);
	
	if(!screen_buffer) {
		enter_text_mode();
		print_string("Ran out of memory trying to allocate the screen buffer!");
		asm("cli");
		while(true) asm("hlt");
	}

	/* set the mouse in the middle of the screen */
	mouse_x = screen_width / 2;
	mouse_y = screen_height / 2;

	/* clear the screen buffer */
	size_t i; for(i = 0; i < screen_width * screen_height; i++)
		screen_buffer[i] = 0;
}
