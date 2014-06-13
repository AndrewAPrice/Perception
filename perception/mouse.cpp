#include "mouse.h"
#include "platform.h"

MouseStatus mouse_status;

void mouse_initialize() {
	for(int i = 0; i < mouse_buttons; i++)
		mouse_status.button[i] = false;

	mouse_status.x = 0;
	mouse_status.y = 0;
	mouse_status.exists = false;

	platform_mouse_initialize();
}

void mouse_connected() {
	mouse_status.exists = true;
}

void mouse_disconnected() {
	mouse_status.exists = false;
}

void mouse_set_position(unsigned int x, unsigned int y) {
	mouse_status.x = x;
	mouse_status.y = y;
}

void mouse_move(int x, int y) {
	int screenWidth = 640;
	int screenHeight = 480;

	mouse_status.x += x;
	mouse_status.y += y;

	if(mouse_status.x < 0)
		mouse_status.x = 0;
	else if(mouse_status.x >= screenWidth)
		mouse_status.x = screenWidth - 1;

	if(mouse_status.y < 0)
		mouse_status.y = 0;
	else if(mouse_status.y >= screenHeight)
		mouse_status.y = screenHeight - 1;
}

void mouse_button_down(int button) {
#ifdef DEBUG
	assert(button < mouse_buttons);
#endif

	mouse_status.button[button] = true;
}

void mouse_button_up(int button) {
#ifdef DEBUG
	assert(button < mouse_buttons);
#endif

	mouse_status.button[button] = false;
}
