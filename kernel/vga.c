#include "pci.h"
#include "vga.h"
#include "video.h"
#include "virtual_allocator.h"

/* references for VGA hardware:
   http://wiki.osdev.org/VGA_Hardware
   http://files.osdev.org/mirrors/geezer/osd/graphics/modes.c

   these are some common VGA modes, we only implement mode 13h to keep things simple:

   05h - 320x200, 4 col, B8000
   0Dh - 320x200, 16 col, A000 - 8 pages
   13h - 320x200, 256 col, A000
   0Fh - 640x350, 1 col, A000 - 2 pages
   10h - 640x350, 16 col, A000
   11h - 640x480, 1 col, A000
   12h - 640x480, 16 col, A000
   07h - 720x400, 1 col, B8000
   14h - 360x400 - 256 coo, ? (special)
   15h - 360x480 - 256 coo, ? (special)
   */

/* vga registers */
#define	VGA_AC_INDEX		0x3C0
#define	VGA_AC_WRITE		0x3C0
#define	VGA_AC_READ		0x3C1
#define	VGA_MISC_WRITE		0x3C2
#define VGA_SEQ_INDEX		0x3C4
#define VGA_SEQ_DATA		0x3C5
#define	VGA_DAC_READ_INDEX	0x3C7
#define	VGA_DAC_WRITE_INDEX	0x3C8
#define	VGA_DAC_DATA		0x3C9
#define	VGA_MISC_READ		0x3CC
#define VGA_GC_INDEX 		0x3CE
#define VGA_GC_DATA 		0x3CF
/*			COLOR emulation		MONO emulation */
#define VGA_CRTC_INDEX		0x3D4		/* 0x3B4 */
#define VGA_CRTC_DATA		0x3D5		/* 0x3B5 */
#define	VGA_INSTAT_READ		0x3DA

#define	VGA_NUM_SEQ_REGS	5
#define	VGA_NUM_CRTC_REGS	25
#define	VGA_NUM_GC_REGS		9
#define	VGA_NUM_AC_REGS		21
#define	VGA_NUM_REGS		(1 + VGA_NUM_SEQ_REGS + VGA_NUM_CRTC_REGS + \
				VGA_NUM_GC_REGS + VGA_NUM_AC_REGS)

#define VGA_MEMORY 0xA0000 + virtual_memory_offset

/* a list of register values to send to the VGA hardware to enter video modes */

/* basic 80x25 text mode */
unsigned char g_80x25_text[] =
{
/* MISC */
	0x67,
/* SEQ */
	0x03, 0x00, 0x03, 0x00, 0x02,
/* CRTC */
	0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
	0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x50,
	0x9C, 0x0E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
	0xFF,
/* GC */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00,
	0xFF,
/* AC */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
	0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
	0x0C, 0x00, 0x0F, 0x08, 0x00
};

/* mode 13h */
unsigned char vga_320x200x256[] =
{
/* MISC */
	0x63,
/* SEQ */
	0x03, 0x01, 0x0F, 0x00, 0x0E,
/* CRTC */
	0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0xBF, 0x1F,
	0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x9C, 0x0E, 0x8F, 0x28,	0x40, 0x96, 0xB9, 0xA3,
	0xFF,
/* GC */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F,
	0xFF,
/* AC */
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x41, 0x00, 0x0F, 0x00,	0x00
};

/* pointer to our memory buffer */
size_t vga_memory_offset;

/* updates the frame buffer poitner based on what mode we're in */
void vga_update_framebuffer_address(void)
{
	unsigned seg;

	outportb(VGA_GC_INDEX, 6);
	seg = inportb(VGA_GC_DATA);
	seg >>= 2;
	seg &= 3;
	switch(seg)
	{
	case 0:
	case 1:
		seg = 0xA0000;
		break;
	case 2:
		seg = 0xB0000;
		break;
	case 3:
		seg = 0xB8000;
		break;
	}

	vga_memory_offset = (size_t)seg + virtual_memory_offset;
}

/* enters a video mode by sending setting the VGA registers */
void vga_write_regs(unsigned char *regs) {
	size_t i;

	/* write MISCELLANEOUS reg */
	outportb(VGA_MISC_WRITE, *regs);
	regs++;
	/* write SEQUENCER regs */
	for(i = 0; i < VGA_NUM_SEQ_REGS; i++)
	{
		outportb(VGA_SEQ_INDEX, i);
		outportb(VGA_SEQ_DATA, *regs);
		regs++;
	}
	/* unlock CRTC registers */
	outportb(VGA_CRTC_INDEX, 0x03);
	outportb(VGA_CRTC_DATA, inportb(VGA_CRTC_DATA) | 0x80);
	outportb(VGA_CRTC_INDEX, 0x11);
	outportb(VGA_CRTC_DATA, inportb(VGA_CRTC_DATA) & ~0x80);
	/* make sure they remain unlocked */
	regs[0x03] |= 0x80;
	regs[0x11] &= ~0x80;
	/* write CRTC regs */
	for(i = 0; i < VGA_NUM_CRTC_REGS; i++)
	{
		outportb(VGA_CRTC_INDEX, i);
		outportb(VGA_CRTC_DATA, *regs);
		regs++;
	}
	/* write GRAPHICS CONTROLLER regs */
	for(i = 0; i < VGA_NUM_GC_REGS; i++)
	{
		outportb(VGA_GC_INDEX, i);
		outportb(VGA_GC_DATA, *regs);
		regs++;
	}
	/* write ATTRIBUTE CONTROLLER regs */
	for(i = 0; i < VGA_NUM_AC_REGS; i++)
	{
		(void)inportb(VGA_INSTAT_READ);
		outportb(VGA_AC_INDEX, i);
		outportb(VGA_AC_WRITE, *regs);
		regs++;
	}
	/* lock 16-color palette and unblank display */
	(void)inportb(VGA_INSTAT_READ);
	outportb(VGA_AC_INDEX, 0x20);
}

/* flips the screen buffer for 8 bpp - 3:3:2 */
void vga_flip_screen_buffer_8(size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t y, x;

	size_t start_index = minx + miny * screen_width;
	size_t i = start_index;
	size_t line_jump = screen_width - (maxx-minx);

	if(dither_screen) {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++, i++) {
				uint32 c = screen_buffer[i];

				uint8 dither_val = dithering_table[x % dithering_table_width + (y % dithering_table_width) + dithering_table_width];
				
				uint8 red = (uint8)((((c >> 16) & 0xFF) + dither_val / 2) * 7 / 255);
				uint8 green = (uint8)((((c >> 8) & 0xFF) + dither_val / 2) * 7 / 255);// + dither_val / 2;
				uint8 blue = (uint8)(((c & 0xFF) + dither_val) * 3 / 255);// + dither_val / 4;

				uint8 val = (red << 5) | (green << 2) | blue;

				((char *)vga_memory_offset)[i] = val;
			}

			i+= line_jump;
		}
	} else {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++, i++) {
				uint32 c = screen_buffer[i];

				uint8 red = (((c >> 16) & 0xFF)) / 32;
				uint8 green = (((c >> 8) & 0xFF)) / 32;
				uint8 blue = (c & 0xFF) / 64;

				uint8 val = (red << 5) | (green << 2) | blue;

				((char *)vga_memory_offset)[i] = val;
			}

			i += line_jump;
		}
	}
}

/* sets up an 8 bpp RGB colour palette using 3:3:2 */
void vga_set_palette() {
	/* set the palette */
	outportb(0x03c8, 0);
	size_t i; for(i = 0; i < 256; i++) {
		/* 8 bit palette will be:
		 rrrgggbb
		 */
		size_t red = ((i >> 5) & 7) * 63 / 7;
		size_t green = ((i >> 2) & 7) * 63 / 7;
		size_t blue = (i & 3) * 63 / 3;

		outportb(0x03c9, (uint8)red);
		outportb(0x03c9, (uint8)green);
		outportb(0x03c9, (uint8)blue);
	}
}

void init_vga(struct PCIDevice *device) {
	vga_write_regs(vga_320x200x256);
	screen_width = 320;
	screen_height = 200;
	//vga_write_pixel = vga_write_pixel8;
	flip_screen_buffer = vga_flip_screen_buffer_8;
	vga_set_palette();

	vga_update_framebuffer_address();
	update_screen_buffer();

	device->driver = 1;
}