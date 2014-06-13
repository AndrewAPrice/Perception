#ifndef WINDOW_H
#define WINDOW_H
struct Process;
struct TurkeyString;

struct Window {
	Window *previous;
	Window *next;

	Process *process;
	TurkeyString *title;

	unsigned int width; /* window width */
	unsigned int height; /* window height */

	void *front_buffer; /* buffer we draw into */
	void *back_buffer; /* back buffer, if 0 then there's no back buffer */
};

extern void window_flip_buffer(Window *window);

#endif
