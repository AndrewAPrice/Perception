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

#define BACKGROUND_COLOUR ((0 << 16) + (78 << 8) + 152)
#define WINDOW_TITLE_HEIGHT 12
#define WINDOW_BORDER_COLOUR 0xFF000000
#define WINDOW_TITLE_TEXT_COLOUR 0xFF000000
#define FOCUSED_DIALOG_COLOUR 0xFF7F7F7F
#define UNFOCUSED_DIALOG_COLOUR 0xFFC3C3C3
#define WINDOW_NO_CONTENTS_COLOUR 0xFFE1E1E1
#define WINDOW_CLOSE_BUTTON_COLOUR 0xFFFF0000
#define UNFOCUSED_WINDOW_COLOUR 0xFF99D9EA
#define FOCUSED_WINDOW_COLOUR 0xFF00A2E8

#define SHELL_BACKGROUND_TINT 0x55000000
#define DRAGGING_WINDOW_DROP_TINT 0x55000000

#define MAX_WINDOW_TITLE_LENGTH 80

#define MOUSE_WIDTH 11
#define MOUSE_HEIGHT 17

#define DIALOG_BORDER_WIDTH 2
#define DIALOG_BORDER_HEIGHT WINDOW_TITLE_HEIGHT + 3

#define SPLIT_FRAME_COLOUR 0xFC3C3C3

struct Thread *window_manager_thread;

struct Window *dialogs_back; /* linked list of dialogs, from back to front */
struct Window *dialogs_front;

struct Window *focused_window; /* the currently focused window */
struct Window *full_screen_window; /* is there a full screened window? */

struct Frame *root_frame; /* top level frame */
struct Frame *last_focused_frame; /* the last focused frame, for figuring out where to open the next window */

bool is_shell_visible; /* is the shell visible? */

bool window_manager_invalidated; /* does the screen need to redraw? */

struct Message *window_manager_next_message; /* queue of window manager messages */
struct Message *window_manager_last_message;

uint16 invalidate_min_x, invalidate_min_y, invalidate_max_x, invalidate_max_y;

uint16 wm_mouse_x, wm_mouse_y;

struct Window *dragging_window;
uint16 dragging_offset_x; /* when dragging a dialog - offset, when dragging a window - top left of the origional title */
uint16 dragging_offset_y;

uint16 dragging_temp_minx, dragging_temp_miny, dragging_temp_maxx, dragging_temp_maxy;

struct Frame *remove_window_from_frame(struct Frame *frame, struct Window *window);

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
	size_t background = BACKGROUND_COLOUR;
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

/* draws a frame */
void draw_frame(struct Frame *frame, uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* skip this frame if it's out of our redraw region */
	if(frame->x >= maxx || frame->y >= maxy ||
		frame->x + frame->width < minx ||
		frame->y + frame->height < miny) {
		return;
	}

	if(frame->is_split_frame) {
		/* split frame */
		if(frame->SplitFrame.is_split_vertically) {
			/* draw middle */
			draw_x_line(frame->x, frame->y + frame->SplitFrame.split_point, frame->width, SPLIT_FRAME_COLOUR,
				screen_buffer, screen_width, screen_height);

			/* draw top */
			if(frame->y + frame->SplitFrame.split_point > miny)
				draw_frame(frame->SplitFrame.child_a, minx, miny, maxx, maxy);

			/* draw bottom */
			if(frame->y + frame->SplitFrame.split_point + 1 < maxy)
				draw_frame(frame->SplitFrame.child_b, minx, miny, maxx, maxy);
		} else {
			/* draw middle */
			draw_y_line(frame->x + frame->SplitFrame.split_point, frame->y, frame->height, SPLIT_FRAME_COLOUR,
				screen_buffer, screen_width, screen_height);

			/* draw left */
			if(frame->x + frame->SplitFrame.split_point > minx)
				draw_frame(frame->SplitFrame.child_a, minx, miny, maxx, maxy);

			/* draw right */
			if(frame->x + frame->SplitFrame.split_point + 1 < maxx)
				draw_frame(frame->SplitFrame.child_b, minx, miny, maxx, maxy);
		}
	} else {
		/* dock frame */
		if(miny < frame->y + frame->height + frame->DockFrame.title_height) {
			/* draw titles */
			fill_rectangle(frame->x, frame->y, frame->x + frame->width, frame->y + frame->DockFrame.title_height, BACKGROUND_COLOUR,
				screen_buffer, screen_width, screen_height);


			uint16 y = frame->y;
			uint16 x = frame->x + 1;

			/* draw title's left border */
			draw_y_line(x, y + 1, WINDOW_TITLE_HEIGHT, WINDOW_BORDER_COLOUR,
				screen_buffer, screen_width, screen_height);

			struct Window *w = frame->DockFrame.first_window;
			while(w) {
				if(frame->width + frame->x <= x + w->title_width + 1) {
					/* draw previous title row's top and bottom border */
					draw_x_line(frame->x, y, x - frame->x, WINDOW_BORDER_COLOUR,
						screen_buffer, screen_width, screen_height);

					/* move to the next line */
					y += WINDOW_TITLE_HEIGHT + 1;
					
					draw_x_line(frame->x, y, x - frame->x, WINDOW_BORDER_COLOUR,
						screen_buffer, screen_width, screen_height);

					x = frame->x + 1;

					/* draw header's left border */
					draw_y_line(x, y + 1, WINDOW_TITLE_HEIGHT, WINDOW_BORDER_COLOUR,
						screen_buffer, screen_width, screen_height);
				}

				/* draw title's right border */
				draw_y_line(x + w->title_width, y + 1, WINDOW_TITLE_HEIGHT, WINDOW_BORDER_COLOUR,
					screen_buffer, screen_width, screen_height);

				/* draw title's background */
				fill_rectangle(x, y + 1, x + w->title_width, y + WINDOW_TITLE_HEIGHT + 1,
					w == frame->DockFrame.focused_window ? FOCUSED_WINDOW_COLOUR : UNFOCUSED_WINDOW_COLOUR,
					screen_buffer, screen_width, screen_height);

				/* write the title */
				draw_string(x + 1, y + 3, w->title, w->title_length, WINDOW_TITLE_TEXT_COLOUR,
					screen_buffer, screen_width, screen_height);


				/* draw the close button */
				if(focused_window == w)
					draw_string(x + w->title_width - 9, y + 3, "X", 1, WINDOW_CLOSE_BUTTON_COLOUR,
						screen_buffer, screen_width, screen_height);
		

				x += w->title_width + 1;

				w = w->next;
			}

			/* draw previous title row's top border */
			draw_x_line(frame->x, y, x - frame->x, WINDOW_BORDER_COLOUR,
				screen_buffer, screen_width, screen_height);

			/* draw bottom border */
			draw_x_line(frame->x, y + WINDOW_TITLE_HEIGHT + 1, frame->width, WINDOW_BORDER_COLOUR,
				screen_buffer, screen_width, screen_height);
		}

		/* draw the focused window */
		draw_window_contents(frame->DockFrame.focused_window, frame->x, frame->y + frame->DockFrame.title_height,
			minx, miny, maxx, maxy);
	}
}

/* draws the shell over the screen */
void draw_shell(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	/* draw the shell buffer */
	draw_sprite(0, 0, shell_buffer, SHELL_WIDTH, screen_height, screen_buffer, screen_width, screen_height, minx, miny, maxx, maxy);

	/* tint the rest of the screen dark */
	if(minx < SHELL_WIDTH) minx = SHELL_WIDTH;
	fill_rectangle_alpha(minx, miny, maxx, maxy, SHELL_BACKGROUND_TINT, screen_buffer, screen_width, screen_height);
}

/* draws drop area when dragging a window */
void draw_dragging_window(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy) {
	uint16 draw_minx = minx < dragging_temp_minx ? dragging_temp_minx : minx;
	uint16 draw_miny = miny < dragging_temp_miny ? dragging_temp_miny : miny;
	uint16 draw_maxx = maxx > dragging_temp_maxx ? dragging_temp_maxx : maxx;
	uint16 draw_maxy = maxy > dragging_temp_maxy ? dragging_temp_maxy : maxy;

	fill_rectangle_alpha(draw_minx, draw_miny, draw_maxx, draw_maxy, DRAGGING_WINDOW_DROP_TINT, screen_buffer, screen_width, screen_height);
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
	} else {
		if(dragging_window && !dragging_window->is_dialog && dragging_temp_maxx) /* dragging a window */
			draw_dragging_window(minx, miny, maxx, maxy);

		if(mouse_is_visible) /* draw the mouse */
			draw_mouse(minx, miny, maxx, maxy);
	}

	flip_screen_buffer(minx, miny, maxx, maxy);
}

/* finds the largest title's width, can ignore a specific window (such as the one we're removing) */
uint16 largest_window_title_width(struct Frame *frame, struct Window *ignore) {
	uint16 largest = 0;

	struct Window *window = frame->DockFrame.first_window;
	while(window != 0) {
		if(window != ignore && window->title_width > largest)
			largest = window->title_width;

		window = window->next;
	}
	return largest;
}

/* gets the area and frame we can drop this window into -
	if the area doesn't match the frame, then turn
	the frame into a split frame */
struct Frame *get_drop_frame(struct Window *window, uint16 *minx, uint16 *miny, uint16 *maxx, uint16 *maxy) {
	if(!root_frame) return 0;

	struct Frame *current_frame = root_frame;

	while(current_frame != 0) {
		/* is this already a split frame? */
		if(current_frame->is_split_frame) {
			if(current_frame->SplitFrame.is_split_vertically) {
				if(mouse_y < current_frame->y + current_frame->SplitFrame.split_point)
					current_frame = current_frame->SplitFrame.child_a;
				else if(mouse_y > current_frame->y + current_frame->SplitFrame.split_point)
					current_frame = current_frame->SplitFrame.child_b;
				else
					return 0; /* on the border */

			} else {
				/* could we deal with being split vertically? will our title fit in the new frame?*/
				if(mouse_x < current_frame->x + current_frame->SplitFrame.split_point) {
					/* can our title fit in this frame? */
					if(window->title_width + 2 < current_frame->SplitFrame.child_a->width)
						current_frame = current_frame->SplitFrame.child_a;
					else
						return 0;
				} else if(mouse_x > current_frame->y + current_frame->SplitFrame.split_point) {
					if(window->title_width + 2 < current_frame->SplitFrame.child_b->width)
						current_frame = current_frame->SplitFrame.child_b;
					else
						return 0;
				} else
					return 0; /* on the border */
			}
		} else {
			uint16 q_width = current_frame->width / 4;
			uint16 q_height = current_frame->height / 4;

			/* split the top */
			if(wm_mouse_x > q_width + current_frame->x &&
				wm_mouse_x < current_frame->x + current_frame->width - q_width &&
				wm_mouse_y < current_frame->y + q_height &&
				current_frame->height > WINDOW_TITLE_HEIGHT * 3) {

				*minx = current_frame->x;
				*miny = current_frame->y;
				*maxx = current_frame->x + current_frame->width;
				*maxy = current_frame->y + current_frame->height / 2;

				return current_frame;
			}

			/* split the bottom */
			if(wm_mouse_x > q_width + current_frame->x &&
				wm_mouse_x < current_frame->x + current_frame->width - q_width &&
				wm_mouse_y > current_frame->y + current_frame->height - q_height &&
				current_frame->height > WINDOW_TITLE_HEIGHT * 3) {

				*minx = current_frame->x;
				*miny = current_frame->y + current_frame->height / 2 + 1;
				*maxx = current_frame->x + current_frame->width;
				*maxy = current_frame->y + current_frame->height;

				return current_frame;
			}

			/* could we deal with being split vertically? will our title fit in the new frame?*/
			bool can_split_vertically =
				(window->title_width + 2) < current_frame->width / 2 - 1 &&
				largest_window_title_width(current_frame, window) + 2 < current_frame->width / 2 - 1;

			if(can_split_vertically) {
				/* split the left */
				if(wm_mouse_y > q_height + current_frame->y &&
					wm_mouse_y < current_frame->y + current_frame->height - q_height &&
					wm_mouse_x < current_frame->x + q_width) {
				
					*minx = current_frame->x;
					*miny = current_frame->y;
					*maxx = current_frame->x + current_frame->width / 2;
					*maxy = current_frame->y + current_frame->height;
					return current_frame;
				}

				/* split the right */
				if(wm_mouse_y > q_height + current_frame->y &&
					wm_mouse_y < current_frame->y + current_frame->height - q_height &&
					wm_mouse_x > current_frame->x + current_frame->width - q_width) {
				
					*minx = current_frame->x + current_frame->width / 2 + 1;
					*miny = current_frame->y;
					*maxx = current_frame->x + current_frame->width;
					*maxy = current_frame->y + current_frame->height;
					return current_frame;
				}
			}

			/* drop here */
			*minx = current_frame->x;
			*miny = current_frame->y;
			*maxx = current_frame->x + current_frame->width;
			*maxy = current_frame->y + current_frame->height;

			return current_frame;

		}
	}

	return 0;
}

/* resizes a window */
void window_resize(struct Window *window) {
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
		} else {
			window->frame->DockFrame.focused_window = window;
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
		} else {
			minx = previous_window->frame->x;
			miny = previous_window->frame->y;
			maxx = previous_window->frame->x + previous_window->frame->width;
			maxy = previous_window->frame->y + previous_window->frame->height;
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
		} else {
			new_win_minx = window->frame->x;
			new_win_miny = window->frame->y;
			new_win_maxx = window->frame->x + window->frame->width;
			new_win_maxy = window->frame->y + window->frame->height;
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

/* updates a frame */
void window_manager_update_frame(struct Frame *frame, bool resized) {
	if(frame->is_split_frame) {
		struct Frame *replace_me = 0; /* child to replace me with if the other child closed */
		if(!frame->SplitFrame.child_a) /* child a closed, promote child b to my position */
			replace_me = frame->SplitFrame.child_b;
		else if(!frame->SplitFrame.child_b) /* child b closed, promote child a to my position */
			replace_me = frame->SplitFrame.child_a;

		if(replace_me) {
			replace_me->x = frame->x;
			replace_me->y = frame->y;
			replace_me->width = frame->width;
			replace_me->height = frame->height;
			replace_me->parent = frame->parent;

			/* replace me in the parent */
			if(frame == root_frame)
				root_frame = replace_me;
			else if(frame->parent->SplitFrame.child_a == frame)
				frame->parent->SplitFrame.child_a = replace_me;
			else
				frame->parent->SplitFrame.child_b = replace_me;


			invalidate_window_manager(frame->x, frame->y, frame->x + frame->width, frame->y + frame->height);

			free(frame);

			/* update this child */
			window_manager_update_frame(replace_me, true);
			return;
		}

		if(resized) {
			/* update the split point */
			if(frame->SplitFrame.is_split_vertically) {
				uint16 split_point = (uint16)((float)frame->height * frame->SplitFrame.split_percent);
				if(split_point != frame->SplitFrame.split_point) {
					frame->SplitFrame.split_point = split_point;
					frame->SplitFrame.child_a->height = split_point;
					frame->SplitFrame.child_a->x = frame->x;
					frame->SplitFrame.child_a->width = frame->width;
					window_manager_update_frame(frame->SplitFrame.child_a, true);

					frame->SplitFrame.child_b->height = frame->height - split_point - 1;
					frame->SplitFrame.child_b->y = frame->y + split_point + 1;
					frame->SplitFrame.child_b->x = frame->x;
					frame->SplitFrame.child_b->width = frame->width;
					window_manager_update_frame(frame->SplitFrame.child_b, true);		
				}
			} else {
				uint16 split_point = (uint16)((float)frame->width * frame->SplitFrame.split_percent);
				if(split_point != frame->SplitFrame.split_point) {

					frame->SplitFrame.split_point = split_point;
					frame->SplitFrame.child_a->width = split_point;
					frame->SplitFrame.child_a->y = frame->y;
					frame->SplitFrame.child_a->height = frame->height;
					window_manager_update_frame(frame->SplitFrame.child_a, true);

					frame->SplitFrame.child_b->width = frame->width - split_point - 1;
					frame->SplitFrame.child_b->x = frame->x + split_point + 1;
					frame->SplitFrame.child_b->y = frame->y;
					frame->SplitFrame.child_b->height = frame->height;
					window_manager_update_frame(frame->SplitFrame.child_b, true);		
				}
			}
			
		}
	} else {
		/* if there's nothing in the frame, delete it */
		if(!frame->DockFrame.first_window) {
			if(root_frame == frame) { /* close the root frame */
				free(frame);
				root_frame = 0;
				invalidate_window_manager(0, 0, screen_width, screen_height);
				return;
			}

			/* remove this from the parent split frame */
			struct Frame *parent = frame->parent;

			if(parent->SplitFrame.child_a == frame)
				parent->SplitFrame.child_a = 0;
			else
				parent->SplitFrame.child_b = 0;
			
			free(frame);
			window_manager_update_frame(parent, false);
			return;
		}

		/* update title height */
		size_t old_title_height = frame->DockFrame.title_height;
		size_t new_title_height = WINDOW_TITLE_HEIGHT + 1; /* top border and initial row */
		size_t titles_on_this_line = 1; /* left border */
		struct Window *w = frame->DockFrame.first_window;
		while(w) {
			if(frame->width > titles_on_this_line + w->title_width + 1)
				titles_on_this_line += w->title_width + 1; /* title and right border */
			else {
				new_title_height += WINDOW_TITLE_HEIGHT + 1;
				titles_on_this_line = 1;
			}

			w = w->next;
		}

		new_title_height++; /* bottom border */
		size_t new_client_height;
		new_client_height = new_title_height > frame->height ? 0 : frame->height - new_title_height;

		if(new_title_height != old_title_height || resized) {
			/* if the title height has changed, resize each window */
			w = frame->DockFrame.first_window;
			while(w) {
				w->x = frame->x;
				w->y = frame->y + new_title_height;
				w->width = frame->width;
				w->height = new_client_height;
				window_resize(w);
				w = w->next;
			}

			frame->DockFrame.title_height = new_title_height;
		}
	}

	invalidate_window_manager(frame->x, frame->y, frame->width, frame->height);
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
	} else {
		/* invalidate this frame */
		minx = window->frame->x;
		miny = window->frame->y;
		maxx = window->frame->x + window->frame->width;
		maxy = window->frame->y + window->frame->height;

		/* unfocus this window */
		if(window == focused_window) {
			if(window->next) /* next window in this frame to focus on? */
				window_manager_focus_window(window->next);
			else if(window->previous) /* previous window? */
				window_manager_focus_window(window->previous);
			else /* unfocus everything */
				window_manager_focus_window(0);
		}

		remove_window_from_frame(window->frame, window);
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

struct Frame *add_window_to_frame(struct Frame *frame, struct Window *window) {
	/* we are a split frame, add us to our largest */
	if(frame->is_split_frame) {
		if(frame->SplitFrame.split_percent > 0.5f)
			return add_window_to_frame(frame->SplitFrame.child_a, window);
		else
			return add_window_to_frame(frame->SplitFrame.child_b, window);
	}

	/* add them to this frame */
	window->next = 0;

	if(frame->DockFrame.first_window) {
		frame->DockFrame.last_window->next = window;
		window->previous = frame->DockFrame.last_window;
		frame->DockFrame.last_window = window;
	} else {
		frame->DockFrame.first_window = window;
		frame->DockFrame.last_window = window;
		window->previous = 0;
		frame->DockFrame.title_height = 0;
	}

	frame->DockFrame.focused_window = window;
	window->frame = frame;

	window_manager_update_frame(frame, false); /* updates the frame's title height */

	window->x = frame->x;
	window->y = frame->y + frame->DockFrame.title_height;
	window->width = frame->width;
	window->height = frame->height - frame->DockFrame.title_height;

	return frame;
}

struct Frame *remove_window_from_frame(struct Frame *frame, struct Window *window) {
	/* remove from the frame */
	if(window->next)
		window->next->previous = window->previous;
	else
		window->frame->DockFrame.last_window = window->previous;

	if(window->previous)
		window->previous->next = window->next;
	else
		window->frame->DockFrame.first_window = window->next;

	if(frame->DockFrame.focused_window == window) { /* was our focused window? */
		if(window->next)
			frame->DockFrame.focused_window = window->next;
		else
			frame->DockFrame.focused_window = window->previous;
	}

	/* invalidate this frame */
	invalidate_window_manager(frame->x, frame->y, frame->x + frame->width, frame->y + frame->height);
	
	window_manager_update_frame(window->frame, false);
}

/* the entry point for the window manager's thread */
void window_manager_thread_entry() {
	/* invalidate the window manager so it draws */
	wm_mouse_x = mouse_x;
	wm_mouse_y = mouse_y;
	invalidate_window_manager(0, 0, screen_width, screen_height);

	create_dialog("An overlapping popup dialog", strlen("An overlapping popup dialog"), 400, 50);
	create_dialog("Another popup dialog", strlen("Another popup dialog"), 200, 200);

	create_window("Test 1", strlen("Test 1"));
	create_window("Crazy!", strlen("Crazy!"));
	create_window("Awesome!", strlen("Awesome!"));
	create_window("So cool", strlen("So cool"));
	create_window("Peter Pan", strlen("Peter Pan"));
	create_window("Raspberry Cake", strlen("Raspberry Cake"));
	create_window("Chocolate Cake", strlen("Chocolate Cake"));
	create_window("Ice Cream Pudding", strlen("Ice Cream Pudding"));
	create_window("Lots of dialogs", strlen("Lots of dialogs"));
	create_window("Woo!", strlen("Woo!"));
	create_window("Shoes", strlen("Shoes"));

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
					} else {
						/* dragging a window */
						uint16 drop_minx, drop_miny, drop_maxx, drop_maxy;
						
						/* are we over the title? */
						if(mouse_x >= dragging_offset_x && mouse_y >= dragging_offset_y &&
							mouse_x <= dragging_offset_x + dragging_window->title_width + 2 &&
							mouse_y <= dragging_offset_y + WINDOW_TITLE_HEIGHT + 2) {
							drop_maxx = 0; /* already here */
						} else {
							/* find a frame to drop it into */
							struct Frame *drop_frame = get_drop_frame(dragging_window, &drop_minx, &drop_miny,
								&drop_maxx, &drop_maxy);

							if(!drop_frame)
								drop_maxx = 0;
						}

						/* see if it changed */
						if(!drop_maxx) {/* no longer dragging */
							if(dragging_temp_maxx) {
								/* was dragging before */
								invalidate_window_manager(dragging_temp_minx, dragging_temp_miny,
									dragging_temp_maxx, dragging_temp_maxy);
								dragging_temp_maxx = 0;
							}
						} else if(!dragging_temp_maxx) {
							/* was never dragging, but now is */
							dragging_temp_minx = drop_minx;
							dragging_temp_miny = drop_miny;
							dragging_temp_maxx = drop_maxx;
							dragging_temp_maxy = drop_maxy;

							invalidate_window_manager(dragging_temp_minx, dragging_temp_miny,
								dragging_temp_maxx, dragging_temp_maxy);
						} else if(drop_minx != dragging_temp_minx ||
							drop_miny != dragging_temp_miny ||
							drop_maxx != dragging_temp_maxx |
							drop_maxy != dragging_temp_maxy) {

							/* was dragging, and now dragging somewhere new */
							invalidate_window_manager(dragging_temp_minx, dragging_temp_miny,
								dragging_temp_maxx, dragging_temp_maxy);
							invalidate_window_manager(drop_minx, drop_miny,
								drop_maxx, drop_maxy);

							dragging_temp_minx = drop_minx;
							dragging_temp_miny = drop_miny;
							dragging_temp_maxx = drop_maxx;
							dragging_temp_maxy = drop_maxy;

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

				uint16 clicked_header = false; /* when dealing with windows, says how far along the header was clicked */

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
							clicked_header = wm_mouse_x - this_window->x;
						} else if(wm_mouse_y >= this_window->y + WINDOW_TITLE_HEIGHT + 1 &&
							wm_mouse_x < this_window->x + this_window->width + 2 &&
							wm_mouse_y < this_window->y + WINDOW_TITLE_HEIGHT + this_window->height + 3) {
							/* clicked the body */
							clicked_window = this_window;
						}
					}
					this_window = this_window->next;
				}


				/* it wasn't a dialog, test the frames */
				struct Frame *clicked_frame = 0;
				if(!clicked_window && root_frame) {
					struct Frame *frame = root_frame;

					bool loop = true;
					while(loop) {
						if(frame->is_split_frame) {
							/* split frame, see what we clicked */
							if(frame->SplitFrame.is_split_vertically) {
								if(wm_mouse_y < frame->y + frame->SplitFrame.split_point)
									frame = frame->SplitFrame.child_a; /* top */
								else if(wm_mouse_y == frame->y + frame->SplitFrame.split_point) {
									/* split point */
									clicked_frame = frame;
									loop = false;
								} else
									frame = frame->SplitFrame.child_b; /* bottom */
							} else {
								if(wm_mouse_x < frame->x + frame->SplitFrame.split_point)
									frame = frame->SplitFrame.child_a; /* left */
								else if(wm_mouse_x == frame->x + frame->SplitFrame.split_point) {
									/* split point */
									clicked_frame = frame;
									loop = false;
								} else
									frame = frame->SplitFrame.child_b; /* right */
							}
						} else {
							/* dock frame */
							loop = false;
							if(wm_mouse_y < frame->y + frame->DockFrame.title_height) {
								/* clicked the title, see who's title we clicked */
								clicked_window = frame->DockFrame.first_window;

								size_t next_title_y = frame->y + WINDOW_TITLE_HEIGHT + 2;
								size_t title_x = frame->x + 1;

								while(!clicked_header && clicked_window) {
									/* loop while we haven't clicked any and there are still windows to test */
									if(title_x + clicked_window->title_width + 1 > frame->width + frame->x) {
										/* next line */
										title_x = frame->x + 1;
										next_title_y += WINDOW_TITLE_HEIGHT + 1;
									}

									if(wm_mouse_y < next_title_y &&
										wm_mouse_y >= next_title_y - WINDOW_TITLE_HEIGHT - 1 &&
										wm_mouse_x < title_x + clicked_window->title_width + 1) {
										clicked_header = wm_mouse_x - title_x; /* clicked this window */

										/* store these values incase we start dragging */
										dragging_offset_x = title_x; /* when dragging a dialog  */
										dragging_offset_y = next_title_y - WINDOW_TITLE_HEIGHT - 1;

										dragging_temp_maxx = 0; /* don't draw the drop area yet */
									} else { /* didn't click it, jump to the next window */
										title_x += clicked_window->title_width + 1;
										clicked_window = clicked_window->next;
									}
								}
							} else /* clicked the body */
								clicked_window = frame->DockFrame.focused_window;
						}
					}
				}

				/* didn't click anything */
				if(clicked_frame) {
					print_string("clicked a frame!");
				} else if(!clicked_window) {
					window_manager_focus_window(0);
					break;
				}

				if(clicked_header) { /* did we click the header? */
					if(clicked_window->is_dialog) {
						if(focused_window == clicked_window &&
							clicked_header >= clicked_window->title_width - 8)
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
						if(focused_window == clicked_window &&
							clicked_header >= clicked_window->title_width - 8)
							window_manager_close_window(clicked_window);
						else {
							/* focus on this window */
							window_manager_focus_window(clicked_window);

							/* if it was the left mouse button, start dragging */
							if(message->window_manager.mouse_event.button == 0)
								dragging_window = focused_window;
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
				if(dragging_window && message->window_manager.mouse_event.button == 0) {
					if(!dragging_window->is_dialog) {
						if(dragging_temp_maxx) { /* is there a drop area visible? */
							invalidate_window_manager(dragging_temp_minx, dragging_temp_miny,
								dragging_temp_maxx, dragging_temp_maxy);
							dragging_temp_maxx = 0;
						}

						struct Frame *drop_frame;
						
						/* test if we can actually drop it now */
						uint16 drop_minx, drop_miny, drop_maxx, drop_maxy;

						if(mouse_x >= dragging_offset_x && mouse_y >= dragging_offset_y &&
							mouse_x <= dragging_offset_x + dragging_window->title_width + 2 &&
							mouse_y <= dragging_offset_y + WINDOW_TITLE_HEIGHT + 2) {
							drop_frame = 0;
						} else
							drop_frame = get_drop_frame(dragging_window, &drop_minx, &drop_miny,
								&drop_maxx, &drop_maxy);

						if(drop_frame) {
							/* yes - there's somewhere we can drop it into! */
							if(drop_minx == drop_frame->x &&
								drop_miny == drop_frame->y &&
								drop_maxx == drop_frame->x + drop_frame->width &&
								drop_maxy == drop_frame->y + drop_frame->height) {
								/* add it to this window */

								if(dragging_window->frame != drop_frame) {
									/* not already part of this window */
									remove_window_from_frame(dragging_window->frame, dragging_window);
									add_window_to_frame(drop_frame, dragging_window);
									last_focused_frame = drop_frame;
								}
							} else if(drop_maxx != drop_frame->x + drop_frame->width) {
								/* drop left */
								/* create a left and right frame */
								struct Frame *child_a = malloc(sizeof(struct Frame));
								if(child_a) {
									struct Frame *child_b = malloc(sizeof(struct Frame));
									if(!child_b)
										free(child_a);
									else {
										remove_window_from_frame(dragging_window->frame, dragging_window);

										/* create two child frames */
										child_a->x = drop_frame->x;
										child_a->y = drop_frame->y;
										child_a->parent = drop_frame;
										child_a->width = drop_frame->width / 2;
										child_a->height = drop_frame->height;
										child_a->is_split_frame = false;
										child_a->DockFrame.first_window = 0;
										child_a->DockFrame.last_window = 0;
										child_a->DockFrame.focused_window = 0;

										child_b->x = drop_frame->x + drop_frame->width / 2 + 1;
										child_b->y = drop_frame->y;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width / 2 - 1;
										child_b->height = drop_frame->height;
										child_b->is_split_frame = false;
										child_b->DockFrame.first_window =
											drop_frame->DockFrame.first_window;
										child_b->DockFrame.last_window =
											drop_frame->DockFrame.last_window;
										child_b->DockFrame.focused_window =
											drop_frame->DockFrame.focused_window;

										/* move this frame's children into child_b */
										struct Window *w = child_b->DockFrame.first_window;
										while(w) {
											w->frame = child_b;
											w = w->next;
										}

										/* turn this frame into a split frame */
										drop_frame->is_split_frame = true;
										drop_frame->SplitFrame.child_a = child_a;
										drop_frame->SplitFrame.child_b = child_b;
										drop_frame->SplitFrame.is_split_vertically = false;
										drop_frame->SplitFrame.split_percent = 0.5f;
										drop_frame->SplitFrame.split_point = 0; /* makes it update */

										add_window_to_frame(child_a, dragging_window);
										last_focused_frame = child_a;

										window_manager_update_frame(drop_frame, true);
									}
								}
							} else if(drop_minx != drop_frame->x) {
								/* drop right */
								/* create a left and right frame */
								struct Frame *child_a = malloc(sizeof(struct Frame));
								if(child_a) {
									struct Frame *child_b = malloc(sizeof(struct Frame));
									if(!child_b)
										free(child_a);
									else {
										remove_window_from_frame(dragging_window->frame, dragging_window);

										/* create two child frames */
										child_a->x = drop_frame->x;
										child_a->y = drop_frame->y;
										child_a->parent = drop_frame;
										child_a->width = drop_frame->width / 2;
										child_a->height = drop_frame->height;
										child_a->is_split_frame = false;
										child_a->DockFrame.first_window =
											drop_frame->DockFrame.first_window;
										child_a->DockFrame.last_window =
											drop_frame->DockFrame.last_window;
										child_a->DockFrame.focused_window =
											drop_frame->DockFrame.focused_window;

										child_b->x = drop_frame->x + drop_frame->width / 2 + 1;
										child_b->y = drop_frame->y;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width / 2 - 1;
										child_b->height = drop_frame->height;
										child_b->is_split_frame = false;
										child_b->DockFrame.first_window = 0;
										child_b->DockFrame.last_window = 0;
										child_b->DockFrame.focused_window = 0;

										/* move this frame's children into child_b */
										struct Window *w = child_a->DockFrame.first_window;
										while(w) {
											w->frame = child_a;
											w = w->next;
										}

										/* turn this frame into a split frame */
										drop_frame->is_split_frame = true;
										drop_frame->SplitFrame.child_a = child_a;
										drop_frame->SplitFrame.child_b = child_b;
										drop_frame->SplitFrame.is_split_vertically = false;
										drop_frame->SplitFrame.split_percent = 0.5f;
										drop_frame->SplitFrame.split_point = 0; /* makes it update */

										add_window_to_frame(child_b, dragging_window);
										last_focused_frame = child_b;

										window_manager_update_frame(drop_frame, true);
									}
								}
							} else if(drop_maxy != drop_frame->y + drop_frame->height) {
								/* drop top */
								/* create a top and bottom frame */
								struct Frame *child_a = malloc(sizeof(struct Frame));
								if(child_a) {
									struct Frame *child_b = malloc(sizeof(struct Frame));
									if(!child_b)
										free(child_a);
									else {
										remove_window_from_frame(dragging_window->frame, dragging_window);

										/* create two child frames */
										child_a->x = drop_frame->x;
										child_a->y = drop_frame->y;
										child_a->parent = drop_frame;
										child_a->width = drop_frame->width;
										child_a->height = drop_frame->height / 2;
										child_a->is_split_frame = false;
										child_a->DockFrame.first_window = 0;
										child_a->DockFrame.last_window = 0;
										child_a->DockFrame.focused_window = 0;

										child_b->x = drop_frame->x;
										child_b->y = drop_frame->y + drop_frame->height / 2 + 1;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width;
										child_b->height = drop_frame->height / 2 - 1;
										child_b->is_split_frame = false;
										child_b->DockFrame.first_window =
											drop_frame->DockFrame.first_window;
										child_b->DockFrame.last_window =
											drop_frame->DockFrame.last_window;
										child_b->DockFrame.focused_window =
											drop_frame->DockFrame.focused_window;

										/* move this frame's children into child_b */
										struct Window *w = child_b->DockFrame.first_window;
										while(w) {
											w->frame = child_b;
											w = w->next;
										}

										/* turn this frame into a split frame */
										drop_frame->is_split_frame = true;
										drop_frame->SplitFrame.child_a = child_a;
										drop_frame->SplitFrame.child_b = child_b;
										drop_frame->SplitFrame.is_split_vertically = true;
										drop_frame->SplitFrame.split_percent = 0.5f;
										drop_frame->SplitFrame.split_point = 0; /* makes it update */

										add_window_to_frame(child_a, dragging_window);
										last_focused_frame = child_a;

										window_manager_update_frame(drop_frame, true);
									}
								}
							} else if(drop_miny != drop_frame->y) {
								/* drop bottom */
								/* create a top and bottom frame */
								struct Frame *child_a = malloc(sizeof(struct Frame));
								if(child_a) {
									struct Frame *child_b = malloc(sizeof(struct Frame));
									if(!child_b)
										free(child_a);
									else {
										remove_window_from_frame(dragging_window->frame, dragging_window);

										/* create two child frames */
										child_a->x = drop_frame->x;
										child_a->y = drop_frame->y;
										child_a->parent = drop_frame;
										child_a->width = drop_frame->width;
										child_a->height = drop_frame->height / 2;
										child_a->is_split_frame = false;
										child_a->DockFrame.first_window =
											drop_frame->DockFrame.first_window;
										child_a->DockFrame.last_window =
											drop_frame->DockFrame.last_window;
										child_a->DockFrame.focused_window =
											drop_frame->DockFrame.focused_window;

										child_b->x = drop_frame->x;
										child_b->y = drop_frame->y + drop_frame->height / 2 + 1;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width;
										child_b->height = drop_frame->height / 2 - 1;
										child_b->is_split_frame = false;
										child_b->DockFrame.first_window = 0;
										child_b->DockFrame.last_window = 0;
										child_b->DockFrame.focused_window = 0;

										/* move this frame's children into child_b */
										struct Window *w = child_a->DockFrame.first_window;
										while(w) {
											w->frame = child_a;
											w = w->next;
										}

										/* turn this frame into a split frame */
										drop_frame->is_split_frame = true;
										drop_frame->SplitFrame.child_a = child_a;
										drop_frame->SplitFrame.child_b = child_b;
										drop_frame->SplitFrame.is_split_vertically = true;
										drop_frame->SplitFrame.split_percent = 0.5f;
										drop_frame->SplitFrame.split_point = 0; /* makes it update */

										add_window_to_frame(child_b, dragging_window);
										last_focused_frame = child_b;

										window_manager_update_frame(drop_frame, true);
									}
								}
							}
						}
					}

					dragging_window = 0;
				}

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
				if(!dialog) {
					free(message->window_manager.create_window.title);
					break;
				}

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
			case WINDOW_MANAGER_MSG_CREATE_WINDOW: {
				struct Window *window = malloc(sizeof(struct Window));
				if(!window) {
					free(message->window_manager.create_window.title);
					break;
				}

				window->title = message->window_manager.create_window.title;
				window->title_length = message->window_manager.create_window.title_length;
				window->title_width = measure_string(window->title, window->title_length) + 15;
				window->is_dialog = false;
				window->buffer = 0;

				/* open this window in the last focused frame */
				if(!last_focused_frame) {
					if(!root_frame) {
						/* create the root frame */
						root_frame = malloc(sizeof(struct Frame));

						if(!root_frame) { /* out of memory */
							free(window->title);
							free(window);
							break;
						}

						root_frame->x = 0;
						root_frame->y = 0;
						root_frame->parent = 0;
						root_frame->width = screen_width;
						root_frame->height = screen_height;
						root_frame->is_split_frame = false;
						root_frame->DockFrame.first_window = 0;
						root_frame->DockFrame.last_window = 0;
						root_frame->DockFrame.focused_window = 0;
					}
					last_focused_frame = root_frame;
				}

				last_focused_frame = add_window_to_frame(last_focused_frame, window);

				/* focus on it */
				focused_window = window;

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
	last_focused_frame = 0;
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
	struct Message *message = allocate_message();
	if(message == 0) return;
	message->window_manager.type = WINDOW_MANAGER_MSG_CREATE_WINDOW;

	/* copy the title across - pulls it out of user space */
	if(title_length > MAX_WINDOW_TITLE_LENGTH) title_length = MAX_WINDOW_TITLE_LENGTH;
	message->window_manager.create_window.title = malloc(title_length);
	if(!message->window_manager.create_window.title) { release_message(message); return; }

	message->window_manager.create_window.title_length = title_length;
	memcpy(message->window_manager.create_window.title, title, title_length);

	window_manager_add_message(message);
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
