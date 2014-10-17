#pragma once

#include "types.h"

struct Process;

#define MSG_USER_REQUESTED_TERMINATION 0
#define MSG_KEY_STATE_CHANGED 1

/* a message that can be sent to processes */
struct Message {
	struct Message *next;

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

} __attribute__((packed));


/* initializes message passing */
extern void init_messages();
/* allocates a message, returns 0 if it failsnext_free_message */
extern struct Message *allocate_message();
/* releases a message, must be allocated with allocate_message */
extern void release_message(struct Message *msg);
/* sends a message, assumes msg.pid is the sender */
extern void send_message(struct Process *to_proc, struct Message *msg);