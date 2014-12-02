#include "draw.h"
#include "font.h"
#include "liballoc.h"
#include "mouse.h"
#include "scheduler.h"
#include "shell.h"
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

struct Thread *window_manager_thread;

struct Window *dialogs_back; /* linked list of dialogs, from back to front */
struct Window *dialogs_front;

struct Window *focused_window; /* the currently focused window */
struct Window *hovering_title; /* the window we are hovering over the title of */
struct Window *full_screen_window; /* is there a full screened window? */

struct Frame *root_frame; /* top level frame */
struct Frame *last_focused; /* the last focused frame, for figuring out where to open the next window */

bool is_shell_visible; /* is the shell visible? */
bool nasdf;

/* draws the background when no window is open */
void draw_background() {
	size_t background = (0 << 16) + (78 << 8) + 152;
	size_t i;
	for(i = 0; i < screen_width * screen_height; i++)
		screen_buffer[i] = background;
}

/* draws the mouse */
void draw_mouse() {
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

	draw_sprite_1bit_alpha(mouse_x, mouse_y, mouse_sprite, 11, 17, screen_buffer, screen_width, screen_height);
}

void draw_window_contents(struct Window *window, uint16 x, uint16 y) {
	if(window->buffer)
		/* we have a buffer, draw it */
		draw_sprite(x, y, window->buffer, window->width, window->height, screen_buffer, screen_width, screen_height);

	else
		fill_rectangle(x, y, window->width, window->height, WINDOW_NO_CONTENTS_COLOUR, screen_buffer, screen_width, screen_height);
}

/* draws the dialogs (floating windows) */
void draw_dialogs() {
	/* draw from back to front */
	struct Window *window = dialogs_back;
	while(window) {
		size_t x = window->x;
		size_t y = window->y;

		/* draw the left border */
		draw_y_line(x, y, WINDOW_TITLE_HEIGHT + window->height + 3, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the borders around the top frame */
		draw_x_line(x, y, window->title_width + 2, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->title_width + 1, y, WINDOW_TITLE_HEIGHT + 1, WINDOW_BORDER_COLOUR,
			screen_buffer, screen_width, screen_height);

		/* fill in the colour behind it */
		fill_rectangle(x + 1, y + 1, window->title_width, WINDOW_TITLE_HEIGHT,
			focused_window==window? FOCUSED_DIALOG_COLOUR : UNFOCUSED_DIALOG_COLOUR,
			screen_buffer, screen_width, screen_height);

		/* write the title */
		draw_string(x + 2, y + 3, window->title, window->title_length, WINDOW_TITLE_TEXT_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the close button */
		if(hovering_title)
			draw_string(x + window->title_width - 8, y + 3, "X", 1, WINDOW_CLOSE_BUTTON_COLOUR,
				screen_buffer, screen_width, screen_height);
		
		y += WINDOW_TITLE_HEIGHT + 1;

		/* draw the rest of the borders */
		draw_x_line(x + 1, y, window->width, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_x_line(x + 1, y + window->height + 1, window->width, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);
		draw_y_line(x + window->width + 1, y, window->height + 2, WINDOW_BORDER_COLOUR, screen_buffer, screen_width, screen_height);

		/* draw the contents */
		draw_window_contents(window, x + 1, y + 1);
		
		window = window->previous;
	}
}

void update_frame(struct Frame *frame) {

}

/* draws a frame */
void draw_frame(struct Frame *frame) {

}

/* draws the shell over the screen */
void draw_shell() {
	/* draw the shell buffer */
	draw_sprite(0, 0, shell_buffer, SHELL_WIDTH, screen_height, screen_buffer, screen_width, screen_height);

	/* tint the rest of the screen dark */
	fill_rectangle_alpha(SHELL_WIDTH, 0, screen_width - SHELL_WIDTH, screen_height, 0x55000000, screen_buffer, screen_width, screen_height);
}

/* draws the screen */
void window_manager_draw() {
	/* draw the windows, or background if there are no windows */
	if(root_frame) {
		if(full_screen_window) {
			/* there's a full screen window, draw it across the whole screen*/
			if(full_screen_window->buffer && !mouse_is_visible && !is_shell_visible) {
				/* we can blit the full screen window directly to the display */
				/* temporarily set the screen buffer to the windows buffer */
				void *old_screen_buffer = screen_buffer;
				screen_buffer = full_screen_window->buffer;
				flip_screen_buffer();
				screen_buffer = old_screen_buffer;
				/* skip everything else */
				return;
			}

			draw_window_contents(full_screen_window, 0, 0);
		} else
			draw_frame(root_frame);
	} else
		draw_background();
	/* draw the dialogs */
	draw_dialogs();

	/* draw the shell */
	if(is_shell_visible) {
		draw_shell();
		draw_mouse(); /* mouse is always visible with the shell */
	} else if(mouse_is_visible) /* draw the mouse */
		draw_mouse();

	flip_screen_buffer();
}

/* the entry point for the window manager's thread */
void window_manager_thread_entry() {
	create_dialog("Hello", strlen("Hello"), 400, 50);

	char *s = "Steve";

	create_dialog(s, strlen(s), 100, 200);

	while(true) {
		window_manager_draw();
	}
	
	while(true) asm("hlt");
}

/* invalidates the window manager, forcing the screen to redraw */
void invalidate_window_manager() {
}

/* initialises the window manager */
void init_window_manager() {
	focused_window = 0; /* no window is focused */
	dialogs_back = dialogs_front = 0;
	hovering_title = 0;
	root_frame = 0;
	full_screen_window = false;
	is_shell_visible = false;

	/* schedule the window manager */
	window_manager_thread = create_thread(0, (size_t)window_manager_thread_entry, 0);
	schedule_thread(window_manager_thread);
}

/* creates a window */
void create_window(char *title, size_t title_length) {
}

/* creates a dialog (floating window) */
void create_dialog(char *title, size_t title_length, uint16 width, uint16 height) {
	struct Window *dialog = malloc(sizeof(struct Window));
	if(!dialog)
		return;

	dialog->title = title;
	if(title_length > 80) title_length = 80; /* cap length of title */
	dialog->title_length = title_length;
	dialog->title_width = measure_string(title, title_length) + 15;
	dialog->is_dialog = true;
	dialog->buffer = 0;

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

	focused_window = dialog;
	hovering_title = dialog;
}

/* toggles the shell */
void toggle_shell() {
	is_shell_visible = !is_shell_visible;
	invalidate_window_manager();
}
