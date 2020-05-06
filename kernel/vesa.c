#include "multiboot2.h"
#include "pci.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "video.h"
#include "virtual_allocator.h"
#include "vesa.h"
#include "vga.h"

uint16 vesa_width = 0;
uint16 vesa_height = 0;
uint16 vesa_bpp = 0;
uint16 vesa_pitch = 0;
size_t vesa_framebuffer = 0;

void handle_vesa_multiboot_header(struct multiboot_tag *tag) {
	struct multiboot_tag_framebuffer *tagfb = (struct multiboot_tag_framebuffer *) tag;
	if(tagfb->common.framebuffer_type == MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
		
		vesa_width = tagfb->common.framebuffer_width;
		vesa_height = tagfb->common.framebuffer_height;
		vesa_bpp = tagfb->common.framebuffer_bpp;
		vesa_pitch = tagfb->common.framebuffer_pitch;
		vesa_framebuffer = tagfb->common.framebuffer_addr;

		print_string("Entered VESA mode during boot: ");
		print_number(vesa_width); print_char('x');
		print_number(vesa_height); print_char('x');
		print_number(vesa_bpp); print_char('\n');
	}
}

size_t vesa_virtual_addr;

/* flip the screen buffer for 15 bits per pixel - 5:5:5 */
void vesa_flip_screen_buffer_15(size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t x; size_t y;

	size_t row_virtual_addr = vesa_virtual_addr;

	size_t start_index = minx + miny * screen_width;
	size_t line_jump = screen_width - (maxx-minx);
	
	uint8 *in = (uint8 *)&screen_buffer[start_index];
	size_t in_line_jump = line_jump * 4;

	uint16 *out = (uint16 *)(row_virtual_addr + minx * 2 + miny * vesa_pitch);
	size_t out_line_jump = vesa_pitch / 2 - (maxx-minx);

	if(dither_screen) {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++) {
				uint8 dither_val = dithering_table[x % dithering_table_width + (y % dithering_table_width) + dithering_table_width];

				uint16 blue = (*in++ + dither_val / 8) * 31 / 255;
				uint16 green = (*in++ + dither_val / 8) * 31 / 255;
				uint16 red = (*in++ + dither_val / 8) * 31 / 255;
				in++;

				uint16 val = (red << 10) | (green << 5) | blue;

				*out = val;
				out++;
			}
			in += in_line_jump;
			out += out_line_jump;
		}
	} else {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++) {
				uint16 blue = *in++ * 32 / 256;
				uint16 green = *in++ * 32 / 256;
				uint16 red = *in++ * 32 / 256;
				in++;

				uint16 val = (red << 10) | (green << 5) | blue;

				*out = val;
				out++;
			}
			in += in_line_jump;
			out += out_line_jump;
		}
	}
}

/* flip the screen buffer for 16 bits per pixel - 5:6:5 */
void vesa_flip_screen_buffer_16(size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t x; size_t y;

	size_t row_virtual_addr = vesa_virtual_addr;

	size_t start_index = minx + miny * screen_width;
	size_t line_jump = screen_width - (maxx-minx);
	
	uint8 *in = (uint8 *)&screen_buffer[start_index];
	size_t in_line_jump = line_jump * 4;

	uint16 *out = (uint16 *)(row_virtual_addr + minx * 2 + miny * vesa_pitch);
	size_t out_line_jump = vesa_pitch / 2 - (maxx-minx);

	if(dither_screen) {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++) {
				uint8 dither_val = dithering_table[x % dithering_table_width + (y % dithering_table_width) + dithering_table_width];
				
				uint16 blue = (*in++ + dither_val / 8) * 31 / 255;
				uint16 green = (*in++ + dither_val / 16) * 63 / 255;
				uint16 red = (*in++ + dither_val / 8) * 31 / 255;
				in++;

				uint16 val = (red << 11) | (green << 5) | blue;

				*out = val;
				out++;
			}
			in += in_line_jump;
			out += out_line_jump;
		}
	} else {
		for(y = miny; y < maxy; y++) {
			for(x = minx; x < maxx; x++) {
				uint16 blue = *in++ * 32 / 256;
				uint16 green = *in++ * 64 / 256;
				uint16 red = *in++ * 32 / 256;
				in++;

				uint16 val = (red << 11) | (green << 5) | blue;

				*out = val;
				out++;
			}
			in += in_line_jump;
			out += out_line_jump;
		}
	}
}

/* flip the screen buffer for 24 bits per pixel - 8:8:8 */
void vesa_flip_screen_buffer_24(size_t minx, size_t miny, size_t maxx, size_t maxy) {
	size_t x; size_t y;

	size_t row_virtual_addr = vesa_virtual_addr;

	size_t start_index = minx + miny * screen_width;
	size_t line_jump = screen_width - (maxx-minx);
	
	uint8 *in = (uint8 *)&screen_buffer[start_index];
	size_t in_line_jump = line_jump * 4;

	uint8 *out = (uint8 *)(row_virtual_addr + minx * 3 + miny * vesa_pitch);
	size_t out_line_jump = vesa_pitch - (maxx-minx) * 3;

	for(y = miny; y < maxy; y++) {
		for(x = minx; x < maxx; x++) {
			*out++ = *in++;
			*out++ = *in++;
			*out++ = *in++;
			in++;
		}
		
		in += in_line_jump;
		out += out_line_jump;
	}

}

void init_vesa(struct PCIDevice *device) {
	/* test if the multiboot struct had a framebuffer */
	if(vesa_width == 0)
		return;

	/* figure out the flipping function to use, at the same time test if this is a valid bpp */
	switch(vesa_bpp) {
		case 15:
			flip_screen_buffer = vesa_flip_screen_buffer_15;
			break;
		case 16:
			flip_screen_buffer = vesa_flip_screen_buffer_16;
			break;
		case 24:
			flip_screen_buffer = vesa_flip_screen_buffer_24;
			break;
		default:
			return;
	}

	
	/* calculate the frame buffer size */
	size_t frame_buffer_size = vesa_pitch * vesa_height;
	/* round up to the number of needed pages */
	size_t frame_buffer_pages = (frame_buffer_size + page_size - 1) / page_size;


	/* find a space to allocate it */
	vesa_virtual_addr = find_free_page_range(kernel_pml4, frame_buffer_pages);
	if(vesa_virtual_addr == 0) return; /* :( */
	/* map the physical memory to virtual memory */
	{
		size_t i = frame_buffer_pages;
		size_t virt = vesa_virtual_addr;
		size_t phys = vesa_framebuffer;
		while(i > 0) {
			map_physical_page(kernel_pml4, virt, phys);
			i--;
			virt += page_size;
			phys += page_size;
		}
	}

	/* set video properties */
	screen_width = vesa_width;
	screen_height = vesa_height;
	update_screen_buffer();

	//vga_write_pixel = vga_write_pixel8;

	device->driver = 1;
}