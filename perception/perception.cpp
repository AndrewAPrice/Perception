#include "perception.h"
#include "platform.h"
#include "mouse.h"
#include "keyboard.h"
#include "process.h"

void kmain() {
	platform_graphics_initialize();
	mouse_initialize();
	keyboard_initialize();
	process_initialize();
	
	/* launch the launcher */
	process_launch_process("Launcher", 8);

	/* the main event loop of the kernel */
	start_scheduling();
}