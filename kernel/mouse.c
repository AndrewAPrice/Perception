#include "io.h"
#include "irq.h"
#include "isr.h"
#include "messages.h"
#include "mouse.h"
#include "text_terminal.h"
#include "video.h"

/* mouse position - the window manager initialises these values to the middle of the screen */
uint16 mouse_x, mouse_y;

/* is the mouse visible? */
bool mouse_is_visible;


/* the mouse's interrupt handler */
struct isr_regs *mouse_handler(struct isr_regs *r) {
	uint8 check;
	uint8 check_against = 1 | (1 << 5); /* byte available, and read from mouse */

	/* buffering what we receive from the mouse */
	uint8 mouse_cycle = 0;
	uint8 mouse_byte[2];

	/* keep looping while here are mouse bytes */
	//if((check = inportb(0x64)) & check_against == check_against) {
	while((check = inportb(0x64)) & check_against == check_against) {
		uint8 val = inportb(0x60);
		if(mouse_cycle == 2) {
			/* read in the last byte */
			mouse_cycle = 0;

			uint8 status = mouse_byte[0];
			uint8 off_x = mouse_byte[1];
			uint8 off_y = val;

			/* offset the mouse */
			int16 mx = mouse_x + off_x - ((status << 4 ) & 0x100);
			int16 my = mouse_y - off_y + ((status << 3 ) & 0x100);

			/* check the bounds of the mouse so it's not off the screen */
			if(mx < 0) mouse_x = 0;
			else if(mx >= screen_width) mouse_x = screen_width - 1;
			else
				mouse_x = mx;

			if(my < 0) mouse_y = 0;
			else if(my >= screen_height) mouse_y = screen_height - 1;
			else
				mouse_y = my;

			/* tell the window manager to redraw the screen, currently does nothing because the window manager
			   is updating in loop */
			if(mouse_is_visible)
				invalidate_window_manager();
		} else  {
			/* read in the first 2 bytes */
			mouse_byte[mouse_cycle]=val;
			mouse_cycle++;
		}
	}

	return r;
}

void mouse_wait(uint8 t) {
	size_t timeout=100000;
	if(t == 0) {
		/* wait for data */
		while(timeout--) {
			if((inportb(0x64) & 1) == 1)
				return;
		}
		return;
	} else {
		/* wait for a signal */
		while(timeout--) {
			if((inportb(0x64) & 2) == 0)
				return;
		}
		return;
	}
}

void mouse_write(uint8 b) {
	mouse_wait(1);
	outportb(0x64, 0xD4);
	mouse_wait(1);
	outportb(0x60, b);
}

uint8 mouse_read() {
	mouse_wait(0);
	return inportb(0x60);
}

/* initialises the mouse */
void init_mouse() {
	mouse_wait(1); outportb(0x64, 0xA8); /* enable auxiliary device */

	/* enable the interrupts */
	mouse_wait(1);
	outportb(0x64, 0x20);
	mouse_wait(0);
	uint8 status = inportb(0x60) | 2;
	mouse_wait(1);
	outportb(0x64, 0x60);
	mouse_wait(1);
	outportb(0x60, status);

	mouse_write(0xF6); mouse_read(); /* set the default values */
	mouse_write(0xF4); /* enable packet streaming */

	irq_install_handler(12, mouse_handler);

	mouse_is_visible = true; /* mouse is initially visible */
}
