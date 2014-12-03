#include "draw.h"
#include "font.h"
#include "liballoc.h"
#include "messages.h"
#include "mouse.h"
#include "scheduler.h"
#include "shell.h"
#include "syscall.h"
#include "thread.h"
#include "video.h"
#include "window_manager.h"

#define WINDOW_TITLE_HEIGHT 12
#define WINDOW_BORDER_COLOUR 0xFF000000
#define WINDOW_TITLE_TEXT_COLOUR 0xFF000000
#define FOCUSED_DIALOG_COLOUR 0xFF7F7F7F
#define UNFOCUSED_DIALOG_COLOUR 0xFFC3C3C3
#define WINDOW_NO_CONTENTS_COLOUR 0xFFE1E1E1
#define WINDOW_CLOSE_BUTTON_COLOUR 0xFFFF0000

#define MAX_WINDOW_TITLE_LENGTH 80

#define MOUSE_WIDTH 11
#define MOUSE_HEIGHT 17

#define DIALOG_BORDER_WIDTH 2
#define DIALOG_BORDER_HEIGHT WINDOW_TITLE_HEIGHT + 3

struct Thread *window_manager_thread;

struct Window *dialogs_back; /* linked list of dialogs, from back to front */
struct Window *dialogs_front;

struct Window *focused_window; /* the currently focused window */
struct Window *full_screen_window; /* is there a full screened window? */

struct Frame *root_frame; /* top level frame */
struct Frame *last_focused; /* the last focused frame, for figuring out where to open the next window */

bool is_shell_visible; /* is the shell visible? */

bool window_manager_invalidated; /* does the screen need to redraw? */

struct Message *window_manager_next_message; /* queue of window manager messages */
struct Message *window_manager_last_message;

uint16 invalidate_min_x, invalidate_min_y, invalidate_max_x, invalidate_max_y;

uint16 wm_mouse_x, wm_mouse_y;

struct Window *dragging_window;
uint16 dragging_offset_x;
uint16 dragging_offset_y;

/* adds a message to the window manager queue */
void window_manager_add_message(struct Message *message) {
	message->next = 0;

	lock_interrupts();

	if(window_manager_next_message)
		window_manager_last_message->next = message;
	else
		window_manager_next_message = message;

	window_manager_last_message = message;
	unlock_interrupts();

	/* wake up the worker thread */
	schedule_thread(window_manager_thread);
}

/* draws the background when no window is open */
void draw_background(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	size_t background = (0 << 16) + (78 << 8) + 152;
	fill_rectangle(minx, miny, maxx, maxy, background, screen_buffer, screen_width, screen_height);
}

/* draws the mouse */
void draw_mouse(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	uint32 mouse_sprite[] = {
		0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0xFF000000, 0xFF000000,
		0xFF000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000, 0x00000000,
		0xFF000000, 0xFF000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFC3C3C3, 0xFF000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFFC3C3C3, 0xFFC3C3C3, 0xFFC3C3C3, 0xFF000000, 0x00000000,
		0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFF000000, 0xFF000000, 0xFF000000, 0x00000000, 0x00000000
	};

	draw_sprite_1bit_alpha(wm_mouse_x, wm_mouse_y, mouse_sprite, MOUSE_WIDTH, MOUSE_HEIGHT, screen_buffer, screen_width, screen_height,
		minx, miny, maxx, maxy);
}

void draw_window_contents(struct Window *window, uint16 x, uint16 y, uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	uint16 draw_minx = minx < x ? x : minx;
	uint16 draw_miny = miny < y ? y : miny;
	uint16 draw_maxx = maxx > x + window->width ? x + window->width : maxx;
	uint16 draw_maxy = maxy > y + window->height ? y + window->height : maxy;

	if(window->buffer)
		/* we have a buffer, draw it */
		draw_sprite(x, y, window->buffer, window->width, window->height, screen_buffer, screen_width, screen_height,
			draw_minx, draw_miny, draw_maxx, draw_maxy);

	else
		fill_rectangle(draw_minx, draw_miny, draw_maxx, draw_maxy, WINDOW_NO_CONTENTS_COLOUR, screen_buffer, screen_width, screen_height);
}

/* draws the dialogs (floating windows) */
void draw_dialogs(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* draw from back to front */
	struct Window *window = dialogs_back;
	while(window) {
		/* skip this dialog if it's out of the redraw region */
		if(window->x >= maxx || window->y >= maxy ||
			window->x + window->width + DIALOG_BORDER_WIDTH < minx ||
			window->y + window->height + DIALOG_BORDER_HEIGHT < miny) {
			window = window->previous;
			continue;
		}

		size_t x = window->x;
		size_t y = window->y;

		/* draw the left border */
		draw_y_line(x, y, WINDOW_TITLE_HEIGHT + window->height + 3, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the borders around the top frame */
		draw_x_line(x, y, window->title_width + 2, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->title_width + 1, y, WINDOW_TITLE_HEIGHT + 1, WINDOW_BORDER_COLOUR,
			screen_buffer, screen_width, screen_height);

		/* fill in the colour behind it */
		fill_rectangle(x + 1, y + 1, window->title_width + x + 1, WINDOW_TITLE_HEIGHT + y + 1,
			focused_window==window? FOCUSED_DIALOG_COLOUR : UNFOCUSED_DIALOG_COLOUR,
			screen_buffer, screen_width, screen_height);

		/* write the title */
		draw_string(x + 2, y + 3, window->title, window->title_length, WINDOW_TITLE_TEXT_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the close button */
		if(focused_window == window)
			draw_string(x + window->title_width - 8, y + 3, "X", 1, WINDOW_CLOSE_BUTTON_COLOUR,
				screen_buffer, screen_width, screen_height);
		
		y += WINDOW_TITLE_HEIGHT + 1;

		/* draw the rest of the borders */
		draw_x_line(x + 1, y, window->width, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_x_line(x + 1, y + window->height + 1, window->width, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->width + 1, y, window->height + 2, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the contents */
		draw_window_contents(window, x + 1, y + 1, minx, miny, maxx, maxy);
		
		window = window->previous;
	}
}

void update_frame(struct Frame *frame) {

}

/* draws a frame */
void draw_frame(struct Frame *frame, uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {

}

/* draws the shell over the screen */
void draw_shell(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* draw the shell buffer */
	draw_sprite(0, 0, shell_buffer, SHELL_WIDTH, screen_height, screen_buffer, screen_width, screen_height, minx, miny, maxx, maxy);

	/* tint the rest of the screen dark */
	if(minx < SHELL_WIDTH) minx = SHELL_WIDTH;
	fill_rectangle_alpha(minx, miny, maxx, maxy, 0x55000000, screen_buffer, screen_width, screen_height);
}

/* draws the screen */
void window_manager_draw(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* draw the windows, or background if there are no windows */
	if(root_frame) {
		if(full_screen_window) {
			/* there's a full screen window, draw it across the whole screen*/
			if(full_screen_window->buffer && !mouse_is_visible && !is_shell_visible) {
				/* we can blit the full screen window directly to the display */
				/* temporarily set the screen buffer to the windows buffer */
				void *old_screen_buffer = screen_buffer;
				screen_buffer = full_screen_window->buffer;
				flip_screen_buffer(minx, miny, maxx, maxy);
				screen_buffer = old_screen_buffer;
				/* skip everything else */
				return;
			}

			draw_window_contents(full_screen_window, 0, 0, minx, miny, maxx, maxy);
		} else
			draw_frame(root_frame, minx, miny, maxx, maxy);
	} else
		draw_background(minx, miny, maxx, maxy);

	/* draw the dialogs */
	draw_dialogs(minx, miny, maxx, maxy);

	/* draw the shell */
	if(is_shell_visible) {
		draw_shell(minx, miny, maxx, maxy);
		draw_mouse(minx, miny, maxx, maxy); /* mouse is always visible with the shell */
	} else if(mouse_is_visible) /* draw the mouse */
		draw_mouse(minx, miny, maxx, maxy);

	flip_screen_buffer(minx, miny, maxx, maxy);
}

/* switches focus to a window */
void window_manager_focus_window(struct Window *window) {
	if(focused_window == window)
		return;

	struct Window *previous_window = focused_window;
	focused_window = window;
	
	if(window) {
		if(window->is_dialog) {
			/* move the dialog to the front */

			/* remove this dialog from the linked list */
			if(window->next)
				window->next->previous = window->previous;
			else
				dialogs_back = window->previous;

			if(window->previous)
				window->previous->next = window->next;
			else
				dialogs_front = window->next;

			/* insert it at the front */
			if(dialogs_front) {
				dialogs_front->previous = window;
				window->next = dialogs_front;
				window->previous = 0;
				dialogs_front = window;
			} else {
				window->previous = 0;
				window->next = 0;
				dialogs_front = window;
				dialogs_back = window;
			}
		}
	}

	/* work out the bounds needed to redraw both windows */
	uint16 minx, miny, maxx, maxy;
	if(previous_window) {
		if(previous_window->is_dialog) {
			minx = previous_window->x;
			miny = previous_window->y;
			maxx = previous_window->x + previous_window->width + DIALOG_BORDER_WIDTH;
			maxy = previous_window->y + previous_window->height + DIALOG_BORDER_HEIGHT;
		}
	}
	if(window) {
		uint16 new_win_minx;
		uint16 new_win_miny;
		uint16 new_win_maxx;
		uint16 new_win_maxy;

		if(window->is_dialog) {
			new_win_minx = window->x;
			new_win_miny = window->y;
			new_win_maxx = window->x + window->width + DIALOG_BORDER_WIDTH;
			new_win_maxy = window->y + window->height + DIALOG_BORDER_HEIGHT;
		}

		if(previous_window) {
			if(new_win_minx < minx)
				minx = new_win_minx;
			if(new_win_miny < miny)
				miny = new_win_miny;
			if(new_win_maxx > maxx)
				maxx = new_win_maxx;
			if(new_win_maxy > maxy)
				maxy = new_win_maxy;
		} else {
			minx = new_win_minx;
			miny = new_win_miny;
			maxx = new_win_maxx;
			maxy = new_win_maxy;
		}
	}

	invalidate_window_manager(minx, miny, maxx, maxy);
}

void window_manager_close_window(struct Window *window) {
	uint16 minx, miny, maxx, maxy;

	/* find the next window to focus, and remove us */
	if(window->is_dialog) {
		minx = window->x;
		miny = window->y;
		maxx = window->x + window->width + DIALOG_BORDER_WIDTH;
		maxy = window->y + window->height + DIALOG_BORDER_HEIGHT;

		if(window == focused_window)
			window_manager_focus_window(window->next);

		if(window->next)
			window->next->previous = window->previous;
		else
			dialogs_back = window->previous;

		if(window->previous)
			window->previous->next = window->next;
		else
			dialogs_front = window->next;
	}

	if(window == dragging_window)
		dragging_window = 0;

	/* free the memory */
	free(window->title);
	free(window);

	/* todo: free the memory buffer */

	/* todo: notify the process their application has closed */

	invalidate_window_manager(minx, miny, maxx, maxy);
}

/* the entry point for the window manager's thread */
void window_manager_thread_entry() {
	/* invalidate the window manager so it draws */
	wm_mouse_x = mouse_x;
	wm_mouse_y = mouse_y;
	invalidate_window_manager(0, 0, screen_width, screen_height);

	create_dialog("Hello", strlen("Hello"), 400, 50);
	create_dialog("Steve", strlen("Steve"), 100, 200);
	create_dialog("Paul", strlen("Paul"), 100, 100);
	create_dialog("Sally", strlen("Sally"), 500, 25);

	/* enter the event loop */
	while(true) {
		sleep_if_not_set((size_t *)&window_manager_next_message);
		/* grab the top value */
		lock_interrupts();

		if(!window_manager_next_message) {
			/* something else woke us up */
			unlock_interrupts();
			continue;
		}

		/* take off the front element from the queue */
		struct Message *message = window_manager_next_message;
		if(message == window_manager_last_message) {
			/* clear the queue */
			window_manager_next_message = 0;
			window_manager_last_message = 0;
		} else
			window_manager_next_message = message->next;

		if(message->window_manager.type == WINDOW_MANAGER_MSG_REDRAW) {
			/* handle redraw messages specially because we want to grab some extra parameters atomaically */
			window_manager_invalidated = false;
			uint16 minx = invalidate_min_x;
			uint16 miny = invalidate_min_y;
			uint16 maxx = invalidate_max_x;
			uint16 maxy = invalidate_max_y;
			unlock_interrupts();
			release_message(message);

			if(maxx > screen_width)
				maxx = screen_width;
			if(maxy > screen_height)
				maxy = screen_height;

			window_manager_draw(minx, miny, maxx, maxy);

			continue;
		}

		unlock_interrupts();

		switch(message->window_manager.type) {
			case WINDOW_MANAGER_MSG_REDRAW:
				break;
			case WINDOW_MANAGER_MSG_MOUSE_MOVE: {
				uint16 minx = wm_mouse_x;
				uint16 miny = wm_mouse_y;
				uint16 maxx = wm_mouse_x;
				uint16 maxy = wm_mouse_y;

				wm_mouse_x = message->window_manager.mouse_event.x;
				wm_mouse_y = message->window_manager.mouse_event.y;

				/* redraw the mouse */
				if(wm_mouse_x < minx) minx = wm_mouse_x;
				if(wm_mouse_y < miny) miny = wm_mouse_y;
				if(wm_mouse_x > maxx) maxx = wm_mouse_x;
				if(wm_mouse_y > maxy) maxy = wm_mouse_y;

				if(mouse_is_visible)
					invalidate_window_manager(minx, miny, maxx + MOUSE_WIDTH, maxy + MOUSE_HEIGHT);

				/* update any window we are dragging */
				if(dragging_window) {
					if(dragging_window->is_dialog) {
						/* dragging a dialog, make sure it doesn't go off the screen */
						uint16 newx = dragging_offset_x > wm_mouse_x ? 0 : wm_mouse_x - dragging_offset_x;
						uint16 newy = dragging_offset_y > wm_mouse_y ? 0 : wm_mouse_y - dragging_offset_y;

						uint16 maxnewx = screen_width - dragging_window->width - DIALOG_BORDER_WIDTH;
						uint16 maxnewy = screen_height - dragging_window->height - DIALOG_BORDER_HEIGHT;

						if(newx > maxnewx) newx = maxnewx;
						if(newy > maxnewy) newy = maxnewy;

						/* update and redraw if it has moved */
						if(newx != dragging_window->x || newy != dragging_window->y) {
							uint16 minx, deltax, miny, deltay;
							if(dragging_window->x < newx) {
								minx = dragging_window->x;
								deltax = newx - dragging_window->x;
							} else {
								minx = newx;
								deltax = dragging_window->x - newx;
							}
							
							if(dragging_window->y < newy) {
								miny = dragging_window->y;
								deltay = newy - dragging_window->y;
							} else {
								miny = newy;
								deltay = dragging_window->y - newy;
							}

							dragging_window->x = newx;
							dragging_window->y = newy;

							invalidate_window_manager(minx, miny,
								minx + dragging_window->width + DIALOG_BORDER_WIDTH + deltax,
								miny + dragging_window->height + DIALOG_BORDER_HEIGHT + deltay);
						}
					}
				}
				break; }
			case WINDOW_MANAGER_MSG_MOUSE_BUTTON_DOWN: {
				uint16 minx = wm_mouse_x;
				uint16 miny = wm_mouse_y;
				uint16 maxx = wm_mouse_x;
				uint16 maxy = wm_mouse_y;

				wm_mouse_x = message->window_manager.mouse_event.x;
				wm_mouse_y = message->window_manager.mouse_event.y;

				/* test the shell */
				if(is_shell_visible) {
					if(wm_mouse_x >= SHELL_WIDTH) {
						/* clicked out of the shell */
						is_shell_visible = false;
						invalidate_window_manager(0, 0, screen_width, screen_height);
					} else {
						/* tell the shell */
					}
					break;
				}

				bool clicked_header = false;

				struct Window *clicked_window = 0;
				/* find out what we clicked on, test dialogs from front to back */
				struct Window *this_window = dialogs_front;
				while(this_window && !clicked_window) {
					if(wm_mouse_x >= this_window->x && wm_mouse_y >= this_window->y) {
						/* test the header */
						if(wm_mouse_x < this_window->x + this_window->title_width + 2 &&
							wm_mouse_y < this_window->y + WINDOW_TITLE_HEIGHT + 2) {
							/* clicked the header */
							clicked_window = this_window;
							clicked_header = true;
						} else if(wm_mouse_y >= this_window->y + WINDOW_TITLE_HEIGHT + 1 &&
							wm_mouse_x < this_window->x + this_window->width + 2 &&
							wm_mouse_y < this_window->y + WINDOW_TITLE_HEIGHT + this_window->height + 3) {
							/* clicked the body */
							clicked_window = this_window;
						}
					}
					this_window = this_window->next;
				}


				/* it wasn't a dialog, test the sframes */
				if(!clicked_window) {
				}

				/* didn't click anything */
				if(!clicked_window) {
					window_manager_focus_window(0);
					break;
				}

				if(clicked_header) { /* did we click the header? */

					/* todo, test if we clicked the 'x' to close the window but only if we're focused */
					if(focused_window == clicked_window && wm_mouse_x >= clicked_window->x + clicked_window->title_width - 8)
						window_manager_close_window(clicked_window);
					else {
						/* focus on this window */
						window_manager_focus_window(clicked_window);

						/* if it was the left mouse button, start dragging */
						if(message->window_manager.mouse_event.button == 0) {
							dragging_window = focused_window;
							dragging_offset_x = wm_mouse_x - clicked_window->x;
							dragging_offset_y = wm_mouse_y - clicked_window->y;
						}
					}
					
				} else {
					/* focus on this window */
					window_manager_focus_window(clicked_window);

					/* test if we clicked inside */
					bool clicked_inside;
					uint16 client_x, client_y;

					if(clicked_window->is_dialog) {
						if(wm_mouse_x >= clicked_window->x + 1 && wm_mouse_y >= clicked_window->y + 2 + WINDOW_TITLE_HEIGHT
							&& wm_mouse_x < clicked_window->x + clicked_window->width + 2 &&
							wm_mouse_y < clicked_window->y + clicked_window->height + 2 + WINDOW_TITLE_HEIGHT) {
							clicked_inside = true;
							client_x = wm_mouse_x - clicked_window->x - 1;
							client_y = wm_mouse_y - clicked_window->y - WINDOW_TITLE_HEIGHT - 2;
						} else
							clicked_inside = false;
					} else {
						clicked_inside = true;
					}
					
					if(clicked_inside) {
						/* send a message to the window */
					}
					/* if we didn't click inside, we clicked the border, do nothing */

				}

				/* redraw the mouse */
				if(wm_mouse_x < minx) minx = wm_mouse_x;
				if(wm_mouse_y < miny) miny = wm_mouse_y;
				if(wm_mouse_x > maxx) maxx = wm_mouse_x;
				if(wm_mouse_y > maxy) maxy = wm_mouse_y;

				if(mouse_is_visible)
					invalidate_window_manager(minx, miny, maxx + MOUSE_WIDTH, maxy + MOUSE_HEIGHT);
				break; }
			case WINDOW_MANAGER_MSG_MOUSE_BUTTON_UP: {
				uint16 minx = wm_mouse_x;
				uint16 miny = wm_mouse_y;
				uint16 maxx = wm_mouse_x;
				uint16 maxy = wm_mouse_y;

				wm_mouse_x = message->window_manager.mouse_event.x;
				wm_mouse_y = message->window_manager.mouse_event.y;
				
				/* stop dragging if we release the mouse button */
				if(dragging_window && message->window_manager.mouse_event.button == 0)
					dragging_window = 0;

				/* redraw the mouse */
				if(wm_mouse_x < minx) minx = wm_mouse_x;
				if(wm_mouse_y < miny) miny = wm_mouse_y;
				if(wm_mouse_x > maxx) maxx = wm_mouse_x;
				if(wm_mouse_y > maxy) maxy = wm_mouse_y;

				if(mouse_is_visible)
					invalidate_window_manager(minx, miny, maxx + MOUSE_WIDTH, maxy + MOUSE_HEIGHT);
				break; }
			case WINDOW_MANAGER_MSG_KEY_EVENT:
				break;
			case WINDOW_MANAGER_MSG_CREATE_DIALOG: {
				struct Window *dialog = malloc(sizeof(struct Window));
				if(!dialog)
					break;

				dialog->title = message->window_manager.create_window.title;
				dialog->title_length = message->window_manager.create_window.title_length;
				dialog->title_width = measure_string(dialog->title, dialog->title_length) + 15;
				dialog->is_dialog = true;
				dialog->buffer = 0;

				uint16 width = message->window_manager.create_window.width;
				uint16 height = message->window_manager.create_window.height;

				/* window can't be smaller than the title */
				if(width < dialog->title_width)
					width = dialog->title_width;

				/* it can't be larger than the screen */
				if(width + 2 > screen_width)
					width = screen_width - 2;

				if(height + WINDOW_TITLE_HEIGHT + 3 > screen_height)
					height = screen_height - WINDOW_TITLE_HEIGHT - 3;

				dialog->width = width;
				dialog->height = height;

				/* centre the new dialog on the screen */
				int32 x = (screen_width - width)/2 - 1;
				int32 y = (screen_height - height)/2 - 2 - WINDOW_TITLE_HEIGHT;
				if(x < 0) x = 0;
				if(y < 0) y = 0;
				dialog->x = x;
				dialog->y = y;


				/* add it to the linked list */
				if(dialogs_front) {
					dialogs_front->previous = dialog;
					dialog->next = dialogs_front;
					dialog->previous = 0;
					dialogs_front = dialog;
				} else {
					dialog->previous = 0;
					dialog->next = 0;
					dialogs_front = dialog;
					dialogs_back = dialog;
				}

				/* focus on it */
				focused_window = dialog;

				invalidate_window_manager(dialog->x, dialog->y,
					dialog->x + dialog->width + DIALOG_BORDER_WIDTH,
					dialog->y + dialog->height + DIALOG_BORDER_HEIGHT);
				break; }
		}

		release_message(message);
	}

}

/* invalidates the window manager, forcing the screen to redraw */
void invalidate_window_manager(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* check if there's another redraw message, so we don't queue up a bunch of redraws */
	lock_interrupts();
	if(window_manager_invalidated) {
		if(minx < invalidate_min_x)
			invalidate_min_x = minx;
		if(miny < invalidate_min_y)
			invalidate_min_y = miny;
		if(maxx > invalidate_max_x)
			invalidate_max_x = maxx;
		if(maxy > invalidate_max_y)
			invalidate_max_y = maxy;
		unlock_interrupts();
		return;
	} else {
		window_manager_invalidated = true;
		invalidate_min_x = minx;
		invalidate_min_y = miny;
		invalidate_max_x = maxx;
		invalidate_max_y = maxy;
	}
	unlock_interrupts();

	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_REDRAW;

	window_manager_add_message(message);
}

/* initialises the window manager */
void init_window_manager() {
	focused_window = 0; /* no window is focused */
	dialogs_back = dialogs_front = 0;
	root_frame = 0;
	full_screen_window = false;
	is_shell_visible = false;
	dragging_window = 0;

	window_manager_next_message = window_manager_last_message = 0;
	window_manager_invalidated = false;

	/* schedule the window manager */
	window_manager_thread = create_thread(0, (size_t)window_manager_thread_entry, 0);
	schedule_thread(window_manager_thread);
}

/* creates a window */
void create_window(char *title, size_t title_length) {
	/* todo - make this queue */
}

/* creates a dialog (floating window) */
void create_dialog(char *title, size_t title_length, uint16 width, uint16 height) {
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_CREATE_DIALOG;

	/* copy the title across - pulls it out of user space */
	if(title_length > MAX_WINDOW_TITLE_LENGTH) title_length = MAX_WINDOW_TITLE_LENGTH;
	message->window_manager.create_window.title = malloc(title_length);
	if(!message->window_manager.create_window.title) { release_message(message); return; }

	message->window_manager.create_window.title_length = title_length;
	memcpy(message->window_manager.create_window.title, title, title_length);

	message->window_manager.create_window.width = width;
	message->window_manager.create_window.height = height;

	window_manager_add_message(message);
}

void window_manager_keyboard_event(uint8 scancode) {
	unsigned char c = scancode & 0x7F;
	if(c == 0x5B || c == 0x5C) { /* windows key, toggle shell */
		if(!(scancode & 0x80)) { /* only toggle if it was pressed */
			is_shell_visible = !is_shell_visible;
			invalidate_window_manager(0, 0, screen_width, screen_height);
		}
		return;
	} else if(c == 0x58) { /* f12, toggle dithering */
		if(!(scancode & 0x80)) { /* only toggle if it was pressed */
			dither_screen = !dither_screen;
			invalidate_window_manager(0, 0, screen_width, screen_height);
		}
		return;
	}

	/* we're still here? send a message to the window manager */
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_KEY_EVENT;
	message->window_manager.key_event.scancode = scancode;

	window_manager_add_message(message);
}

/* handles a mouse button being clicked */
void window_manager_mouse_down(uint16 x, uint16 y, uint8 button) {
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_MOUSE_BUTTON_DOWN;
	message->window_manager.mouse_event.x = x;
	message->window_manager.mouse_event.y = y;
	message->window_manager.mouse_event.button = button;

	window_manager_add_message(message);
}

/* handles a mouse button being released */
void window_manager_mouse_up(uint16 x, uint16 y, uint8 button) {
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_MOUSE_BUTTON_UP;
	message->window_manager.mouse_event.x = x;
	message->window_manager.mouse_event.y = y;
	message->window_manager.mouse_event.button = button;

	window_manager_add_message(message);
}

/* handles the mouse moving */
void window_manager_mouse_move(uint16 x, uint16 y) {
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_MOUSE_MOVE;
	message->window_manager.mouse_event.x = x;
	message->window_manager.mouse_event.y = y;

	window_manager_add_message(message);
}
