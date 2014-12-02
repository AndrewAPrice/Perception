#pragma once
#include "types.h"

struct PCIDevice;

#define VIDEO_MODE_BPP_1 0
#define VIDEO_MODE_BPP_2 1
#define VIDEO_MODE_BPP_4 2
#define VIDEO_MODE_BPP_8 3
#define VIDEO_MODE_BPP_15 4
#define VIDEO_MODE_BPP_16 5
#define VIDEO_MODE_BPP_24 6
#define VIDEO_MODE_BPP_32 7

struct VideoMode {
	uint16 width;
	uint16 height;
	uint8 supported_bpp; /* bitmap of supported widths */
};

extern size_t screen_width;
extern size_t screen_height;
extern uint32 *screen_buffer;
extern bool dither_screen;
extern uint8 dithering_table[];
#define dithering_table_width 8

extern void (*flip_screen_buffer)();

extern void init_video();
extern void init_video_device(struct PCIDevice *device);
extern void check_for_video();
extern void update_screen_buffer();
