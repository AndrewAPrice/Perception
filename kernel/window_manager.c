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
#include "wm_drawing.h"


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
struct Frame *dragging_frame;
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
				current_frame->height > WINDOW_MINIMUM_HEIGHT) {

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
				current_frame->height > WINDOW_MINIMUM_HEIGHT) {

				*minx = current_frame->x;
				*miny = current_frame->y + current_frame->height / 2 + SPLIT_BORDER_WIDTH;
				*maxx = current_frame->x + current_frame->width;
				*maxy = current_frame->y + current_frame->height;

				return current_frame;
			}

			/* could we deal with being split horizontally? will our title fit in the new frame?*/
			bool can_split_horizontally =
				(window->title_width + 2) < current_frame->width / 2 - SPLIT_BORDER_WIDTH &&
				largest_window_title_width(current_frame, window) + 2 < current_frame->width / 2 - SPLIT_BORDER_WIDTH;

			if(can_split_horizontally) {
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
				
					*minx = current_frame->x + current_frame->width / 2 + SPLIT_BORDER_WIDTH;
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

/* checks if this is a valid size for a frame */
bool window_manager_is_valid_size_for_frame(struct Frame *frame, uint16 width, uint16 height) {
	if(frame->is_split_frame) {
		if(frame->SplitFrame.is_split_vertically) {
			uint16 new_split_point = (uint16)((float)height * frame->SplitFrame.split_percent);
			uint16 h1 = new_split_point;
			uint16 h2 = frame->height - new_split_point - SPLIT_BORDER_WIDTH;

			return window_manager_is_valid_size_for_frame(frame->SplitFrame.child_a, frame->width, h1) &&
				window_manager_is_valid_size_for_frame(frame->SplitFrame.child_b, frame->width, h2);
		} else {
			uint16 new_split_point = (uint16)((float)width * frame->SplitFrame.split_percent);
			uint16 w1 = new_split_point;
			uint16 w2 = frame->width - new_split_point - SPLIT_BORDER_WIDTH;

			return window_manager_is_valid_size_for_frame(frame->SplitFrame.child_a, w1, frame->height) &&
				window_manager_is_valid_size_for_frame(frame->SplitFrame.child_b, w2, frame->height);
		}

	} else {
		if(height <= WINDOW_MINIMUM_HEIGHT)
			return false;

		/* make sure titles fit in this window */
		uint16 largest_title_width = largest_window_title_width(frame, 0);
		return largest_title_width + 2 < width;

	}
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
			if(dragging_frame) {
				dragging_frame = 0;
				dragging_temp_maxx = 0;
			}

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
				//if(split_point != frame->SplitFrame.split_point) {
					frame->SplitFrame.split_point = split_point;
					frame->SplitFrame.child_a->x = frame->x;
					frame->SplitFrame.child_a->y = frame->y;
					frame->SplitFrame.child_a->width = frame->width;
					frame->SplitFrame.child_a->height = split_point;
					window_manager_update_frame(frame->SplitFrame.child_a, true);

					frame->SplitFrame.child_b->x = frame->x;
					frame->SplitFrame.child_b->y = frame->y + split_point + SPLIT_BORDER_WIDTH;
					frame->SplitFrame.child_b->width = frame->width;
					frame->SplitFrame.child_b->height = frame->height - split_point - SPLIT_BORDER_WIDTH;
					window_manager_update_frame(frame->SplitFrame.child_b, true);
				//}
			} else {
				uint16 split_point = (uint16)((float)frame->width * frame->SplitFrame.split_percent);
				//if(split_point != frame->SplitFrame.split_point) {
					frame->SplitFrame.split_point = split_point;
					frame->SplitFrame.child_a->x = frame->x;
					frame->SplitFrame.child_a->y = frame->y;
					frame->SplitFrame.child_a->width = split_point;
					frame->SplitFrame.child_a->height = frame->height;
					window_manager_update_frame(frame->SplitFrame.child_a, true);

					frame->SplitFrame.child_b->x = frame->x + split_point + SPLIT_BORDER_WIDTH;
					frame->SplitFrame.child_b->y = frame->y;
					frame->SplitFrame.child_b->width = frame->width - split_point - SPLIT_BORDER_WIDTH;
					frame->SplitFrame.child_b->height = frame->height;
					window_manager_update_frame(frame->SplitFrame.child_b, true);		
				//}
			}

			invalidate_window_manager(frame->x, frame->y, frame->x + frame->width, frame->y + frame->height);

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

/* resizes the split - set determines if it should be set in place, or merely show the scroll*/
void window_manager_drag_split(bool set) {

	/* currently drawing a drop box */
	if(dragging_temp_maxx) /* invalidate where it used to be */
		invalidate_window_manager(dragging_temp_minx,dragging_temp_miny,dragging_temp_maxx,dragging_temp_maxy);

	bool is_valid_size = false;

	if(dragging_frame->SplitFrame.is_split_vertically) {
		dragging_temp_minx = dragging_frame->x;
		dragging_temp_maxx = dragging_frame->x + dragging_frame->width;
		dragging_temp_miny = wm_mouse_y;
		dragging_temp_maxy = wm_mouse_y + SPLIT_BORDER_WIDTH;

		if(wm_mouse_y > dragging_frame->y &&
			wm_mouse_y < dragging_frame->y + dragging_frame->height - SPLIT_BORDER_WIDTH)
			is_valid_size = window_manager_is_valid_size_for_frame(dragging_frame->SplitFrame.child_a,
				dragging_frame->width, wm_mouse_y - dragging_frame->y) &&
				window_manager_is_valid_size_for_frame(dragging_frame->SplitFrame.child_b,
					dragging_frame->width, dragging_frame->y + dragging_frame->height - wm_mouse_y - SPLIT_BORDER_WIDTH);
	} else {
		dragging_temp_minx = wm_mouse_x;
		dragging_temp_maxx = wm_mouse_x + SPLIT_BORDER_WIDTH;
		dragging_temp_miny = dragging_frame->y;
		dragging_temp_maxy = dragging_frame->y + dragging_frame->height;

		if(wm_mouse_x > dragging_frame->x &&
			wm_mouse_x < dragging_frame->x + dragging_frame->width - SPLIT_BORDER_WIDTH)
			is_valid_size = window_manager_is_valid_size_for_frame(dragging_frame->SplitFrame.child_a,
				wm_mouse_x - dragging_frame->x, dragging_frame->height) &&
			window_manager_is_valid_size_for_frame(dragging_frame->SplitFrame.child_b,
				dragging_frame->x + dragging_frame->width - wm_mouse_x - SPLIT_BORDER_WIDTH, dragging_frame->height);
	}


	if(set) {
		dragging_temp_maxx = 0;

		if(is_valid_size) {
			if(dragging_frame->SplitFrame.is_split_vertically)
				dragging_frame->SplitFrame.split_percent = (float)(wm_mouse_y - dragging_frame->y) / (float)dragging_frame->height;
			else
				dragging_frame->SplitFrame.split_percent = (float)(wm_mouse_x - dragging_frame->x) / (float)dragging_frame->width;

			window_manager_update_frame(dragging_frame, true);
		}

		dragging_frame = false;
	} else {

		/* draw a drop box */
		if(is_valid_size) /* invalidate where it now is */
			invalidate_window_manager(dragging_temp_minx,dragging_temp_miny,dragging_temp_maxx,dragging_temp_maxy);
		else
			dragging_temp_maxx = 0;
	}
}

void window_manager_close_window(struct Window *window) {
	uint16 minx, miny, maxx, maxy;

	/* find the next window to focus, and remove us */
	if(window->is_dialog) {
		minx = window->x;
		miny = window->y;
		maxx = window->x + window->width + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH;
		maxy = window->y + window->height + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH;

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

	create_window("Test 1", strlen("Test 1"));
	create_window("Test 1", strlen("Test 1"));
	create_window("Test 1", strlen("Test 1"));
	create_window("Test 1", strlen("Test 1"));
	create_window("Test 1", strlen("Test 1"));
	create_window("Test 1", strlen("Test 1"));

	create_dialog("Test 1", strlen("Test 1"), 200, 100);
	create_dialog("Test 2", strlen("Test 2"), 100, 200);

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

						uint16 maxnewx = screen_width - dragging_window->width - DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH;
						uint16 maxnewy = screen_height - dragging_window->height - DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH;

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
								minx + dragging_window->width + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH + deltax,
								miny + dragging_window->height + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH + deltay);
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
				} else if(dragging_frame)
					window_manager_drag_split(false);
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
								else if(wm_mouse_y < frame->y + frame->SplitFrame.split_point + SPLIT_BORDER_WIDTH) {
									/* split point */
									clicked_frame = frame;
									loop = false;
								} else
									frame = frame->SplitFrame.child_b; /* bottom */
							} else {
								if(wm_mouse_x < frame->x + frame->SplitFrame.split_point)
									frame = frame->SplitFrame.child_a; /* left */
								else if(wm_mouse_x < frame->x + frame->SplitFrame.split_point + SPLIT_BORDER_WIDTH) {
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
					dragging_frame = clicked_frame;
					break;
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

										child_b->x = drop_frame->x + drop_frame->width / 2 + SPLIT_BORDER_WIDTH;
										child_b->y = drop_frame->y;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width / 2 - SPLIT_BORDER_WIDTH;
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

										child_b->x = drop_frame->x + drop_frame->width / 2 + SPLIT_BORDER_WIDTH;
										child_b->y = drop_frame->y;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width / 2 - SPLIT_BORDER_WIDTH;
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
										child_b->y = drop_frame->y + drop_frame->height / 2 + SPLIT_BORDER_WIDTH;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width;
										child_b->height = drop_frame->height / 2 - SPLIT_BORDER_WIDTH;
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
										child_b->y = drop_frame->y + drop_frame->height / 2 + SPLIT_BORDER_WIDTH;
										child_b->parent = drop_frame;
										child_b->width = drop_frame->width;
										child_b->height = drop_frame->height / 2 - SPLIT_BORDER_WIDTH;
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
				} if(dragging_frame && message->window_manager.mouse_event.button == 0)
					window_manager_drag_split(true);

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
				int32 x = (screen_width - width)/2 - SPLIT_BORDER_WIDTH;
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
					dialog->x + dialog->width + DIALOG_BORDER_WIDTH + DIALOG_SHADOW_WIDTH,
					dialog->y + dialog->height + DIALOG_BORDER_HEIGHT + DIALOG_SHADOW_WIDTH);
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
	dragging_frame = 0;

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
