#pragma once

#include "types.h"

struct Process;

#define MSG_USER_REQUESTED_TERMINATION 0
#define MSG_KEY_STATE_CHANGED 1

/* a generic message, used internally everywhere! */
struct Message {
	struct Message *next;

	union {
		/* a message for a process */
		struct {
			uint64 pid;
			uint32 type;
			union {
				struct message_payload {
					unsigned char payload_bytes[0];
				} __attribute__((packed)) payload;
				struct message_key_state_changed {
					unsigned char scancode;
				} __attribute__((packed)) key_state_changed;
			};
		} message_to_process;

		/* a message for the window manager */
		struct {
			#define WINDOW_MANAGER_MSG_REDRAW 0
			#define WINDOW_MANAGER_MSG_MOUSE_MOVE 1
			#define WINDOW_MANAGER_MSG_MOUSE_BUTTON_DOWN 2
			#define WINDOW_MANAGER_MSG_MOUSE_BUTTON_UP 3
			#define WINDOW_MANAGER_MSG_KEY_EVENT 4
			#define WINDOW_MANAGER_MSG_CREATE_DIALOG 5
			#define WINDOW_MANAGER_MSG_CREATE_WINDOW 6
			uint8 type;
			union {
				struct {
					uint8 scancode;
				} key_event;
				struct {
					uint16 x;
					uint16 y;
					uint8 button; /* the button number, only if it's a button down/up */
				} mouse_event;
				struct {
					char *title;
					size_t title_length;
					uint16 width; /* dialog only */
					uint16 height; /* dialog only */
					/* todo - some callback to the process that requested this */
				} create_window;

			};
		} window_manager;
	};

} __attribute__((packed));


/* initializes message passing */
extern void init_messages();
/* allocates a message, returns 0 if it failsnext_free_message */
extern struct Message *allocate_message();
/* releases a message, must be allocated with allocate_message */
extern void release_message(struct Message *msg);
/* sends a message, assumes msg.pid is the sender */
extern void send_message(struct Process *to_proc, struct Message *msg);