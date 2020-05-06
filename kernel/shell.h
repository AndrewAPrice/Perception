#pragma once
#include "types.h"

/* the width of the shell on the screen */
#define SHELL_WIDTH 200

extern uint32 *shell_buffer; /* the buffer the shell draws into */

/* initialises the shell */
extern void init_shell();

/* the entry point for the shell's thread */
extern void shell_entry();

/* notifies the shell that a new disk has been mounted, so we can scan it for applications */
extern void shell_disk_mounted();
