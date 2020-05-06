#include "mouse.h"
#include "shell.h"
#include "video.h"
#include "window_manager.h"
#include "wm_drawing.h"

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

void draw_header_background(uint16 x, uint16 y, uint16 width, uint32 colour) {
	uint32 outer_line = colour - 0x101010;
	
	draw_x_line(x, y, width, outer_line, screen_buffer, screen_width, screen_height);
	fill_rectangle(x, y + 1, width + x, WINDOW_TITLE_HEIGHT + y - 1, colour, screen_buffer, screen_width, screen_height);
	draw_x_line(x, y + WINDOW_TITLE_HEIGHT - 1 , width, outer_line, screen_buffer, screen_width, screen_height);
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
		draw_y_line(x, y, WINDOW_TITLE_HEIGHT + window->height + 3, DIALOG_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the borders around the top frame */
		draw_x_line(x, y, window->title_width + 2, DIALOG_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->title_width + 1, y, WINDOW_TITLE_HEIGHT + 1, DIALOG_BORDER_COLOUR,
			screen_buffer, screen_width, screen_height);

		/* draw shadows */
		draw_y_line_alpha(x + window->title_width + 2, y + 1, WINDOW_TITLE_HEIGHT, DIALOG_SHADOW_0,
			screen_buffer, screen_width, screen_height);
		draw_y_line_alpha(x + window->title_width + 3, y + 2, WINDOW_TITLE_HEIGHT - 1, DIALOG_SHADOW_1,
			screen_buffer, screen_width, screen_height);

		/* fill in the colour behind it */
		draw_header_background(x + 1, y + 1, window->title_width,
			focused_window==window? FOCUSED_WINDOW_COLOUR : UNFOCUSED_WINDOW_COLOUR);

		/* write the title */
		draw_string(x + 2, y + 3, window->title, window->title_length, WINDOW_TITLE_TEXT_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the close button */
		if(focused_window == window)
			draw_string(x + window->title_width - 8, y + 3, "X", 1, WINDOW_CLOSE_BUTTON_COLOUR,
				screen_buffer, screen_width, screen_height);

		y += WINDOW_TITLE_HEIGHT + 1;

		/* draw the rest of the borders */
		draw_x_line(x + 1, y, window->width, DIALOG_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_x_line(x + 1, y + window->height + 1, window->width, DIALOG_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->width + 1, y, window->height + 2, DIALOG_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw shadows */
		draw_x_line_alpha(x + 2, y + window->height + 2, window->width + 1, DIALOG_SHADOW_0, screen_buffer, screen_width, screen_height);
		draw_x_line_alpha(x + 3, y + window->height + 3, window->width + 1, DIALOG_SHADOW_1, screen_buffer, screen_width, screen_height);
		draw_y_line_alpha(x + window->width + 2, y + 1, window->height + 1, DIALOG_SHADOW_0, screen_buffer, screen_width, screen_height);
		draw_y_line_alpha(x + window->width + 3, y + 2, window->height + 1, DIALOG_SHADOW_1, screen_buffer, screen_width, screen_height);
				
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
			draw_x_line(frame->x, frame->y + frame->SplitFrame.split_point, frame->width, SPLIT_BORDER_COL_0,
				screen_buffer, screen_width, screen_height);
			draw_x_line(frame->x, frame->y + frame->SplitFrame.split_point + 1, frame->width, SPLIT_BORDER_COL_1,
				screen_buffer, screen_width, screen_height);

			/* draw top */
			if(frame->y + frame->SplitFrame.split_point > miny)
				draw_frame(frame->SplitFrame.child_a, minx, miny, maxx, maxy);

			/* draw bottom */
			if(frame->y + frame->SplitFrame.split_point + 1 < maxy)
				draw_frame(frame->SplitFrame.child_b, minx, miny, maxx, maxy);
		} else {
			/* draw middle */
			draw_y_line(frame->x + frame->SplitFrame.split_point, frame->y, frame->height, SPLIT_BORDER_COL_0,
				screen_buffer, screen_width, screen_height);
			draw_y_line(frame->x + frame->SplitFrame.split_point + 1, frame->y, frame->height, SPLIT_BORDER_COL_1,
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
				draw_header_background(x, y + 1, w->title_width,
					focused_window == w ? FOCUSED_WINDOW_COLOUR :
					w == frame->DockFrame.focused_window ? UNFOCUSED_WINDOW_COLOUR : UNSELECTED_WINDOW_COLOUR);

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
	draw_sprite_alpha(0, 0, shell_buffer, SHELL_WIDTH, screen_height, screen_buffer, screen_width, screen_height, minx, miny, maxx, maxy);

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

/* draws the screen - only updates whats within the bounds of the parameters */
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
		if(dragging_temp_maxx) /* dragging a window or split */
			draw_dragging_window(minx, miny, maxx, maxy);

		if(mouse_is_visible) /* draw the mouse */
			draw_mouse(minx, miny, maxx, maxy);
	}

	flip_screen_buffer(minx, miny, maxx, maxy);
}