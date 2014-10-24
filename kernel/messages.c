#include "messages.h"
#include "liballoc.h"
#include "process.h"

struct Message *next_free_message;

size_t allocated_messages = 0; /* total number of allocated messages */
size_t free_messages = 0; /* total number of free messages */

const size_t max_free_messages = 100; /* the maximum number of free messages until we start deleting some */

const size_t max_messages_per_process = 100; /* the maximum number of messages we'll let processes queue up */

/* initializes message passing */
void init_messages() {
	next_free_message = 0;
}

/* allocates a message, returns 0 if it fails */
struct Message *allocate_message() {
	if(next_free_message == 0) {
		/* no free messages, need to allocate one */
		struct Message *msg = (struct Message *)malloc(sizeof (struct Message));
		
		if(msg != 0) /* incremented number of allocated message */
			allocated_messages++;

		return msg;
	} else {
		struct Message *msg = next_free_message;
		next_free_message = next_free_message->next;

		free_messages--;
		return msg;
	}

}

/* releases a message, must be allocated with allocate_message */
void release_message(struct Message *msg) {
	if(free_messages >= max_free_messages) {
		/* release thie mssage if our list of free messages is long enough */
		free(msg);
		allocated_messages--;
	} else {
		/* add this message onto our list of free messages */
		msg->next = next_free_message;
		next_free_message = msg;

		free_messages++;
	}
}

/* sends a message, assumes msg.pid is the sender */
void send_message(struct Process *to_proc, struct Message *msg) {
	/* if this process's message list is too long, release it */
	if(to_proc->messages >= max_messages_per_process) {
		release_message(msg);
		return;
	}

	to_proc->messages++;
	
	/* add to the linked list of messages */
	msg->next = 0;
	if(to_proc->last_message == 0) {
		/* no messages */
		to_proc->next_message = msg;
		to_proc->last_message = msg;

	} else {
		/* add to the end */
		to_proc->last_message->next = msg;
		to_proc->last_message = msg;
	}

	/* if there is a sleeping thread to notify, wake it */
	if(to_proc->waiting_thread) {
		/* todo, wake this thread */
	}
}
