#ifndef MOUSE_H
#define MOUSE_H

const unsigned int mouse_buttons = 5; /* maximum number of supported mouse buttons */

struct MouseStatus {
	int x; /* x position of the cursor */
	int y; /* y position of the cursor */
	bool button[mouse_buttons];
	bool exists; /* is a mouse connected */
};

extern MouseStatus mouse_status;
extern void mouse_initialize();
extern void mouse_connected();
extern void mouse_disconnected();
extern void mouse_set_position(unsigned int x, unsigned int y);
extern void mouse_move(int x, int y);
extern void mouse_button_down(int button);
extern void mouse_button_up(int button);

#endif
