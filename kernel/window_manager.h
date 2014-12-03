#pragma once
#include "types.h"

struct Frame;

/* a window that programs can draw into */
struct Window {
	char *title;
	size_t title_length; /* in characters */
	uint16 title_width; /* in pixels */

	uint16 x, y, width, height; /* window position and size */
	bool is_dialog; /* is this a dialog? */

	struct Frame *frame; /* the frame this is in, not used in dialogs */

	void *buffer; /* frame buffer, null if it isn't allocated */

	struct Window *next;
	struct Window *previous;
};

/* a frame is what windows dock to */
struct Frame {
	uint16 x, y, width, height; /* frame position and size */
	struct Frame *parent;

	bool is_split_frame; /* is this a split frame or a dock frame? */
	union {
		struct {
			struct Frame *child_a;
			struct Frame *child_b;
			bool is_split_vertically; /* direction it is split */
			float split_percent; /* split percentage */
			uint16 split_point; /* position of the split in pixels */
		} SplitFrame;
		struct {
			uint16 title_height; /* the title height with all of the windows in them */

			/* linked list of windows in this frame*/
			struct Window *first_window;
			struct Window *last_window;

			/* the currently focused window */
			struct Window *focused_window;
		} DockFrame;
	};
};

/* invalidates the window manager, forcing the screen to redraw */
extern void invalidate_window_manager(uint16 minx, uint16 miny, uint16 maxx, uint16 maxy);

/* initialises the window manager */
extern void init_window_manager();

/* creates a window */
extern void create_window(char *title, size_t title_length);

/* creates a dialog (floating window) */
extern void create_dialog(char *title, size_t title_length, uint16 width, uint16 height);

/* handles a keyboard event */
extern void window_manager_keyboard_event(uint8 scancode);

/* handles a mouse button being clicked */
extern void window_manager_mouse_down(uint16 x, uint16 y, uint8 button);

/* handles a mouse button being released */
extern void window_manager_mouse_up(uint16 x, uint16 y, uint8 button);

/* handles the mouse moving */
extern void window_manager_mouse_move(uint16 x, uint16 y);
