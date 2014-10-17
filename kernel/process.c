#include "process.h"
#include "liballoc.h"
#include "virtual_allocator.h"
#include "messages.h"

size_t last_assigned_pid = 0; /* the last assigned pid */
struct Process *first_process; /* linked list of processes */

/* initializes internal structures for tracking processes */
void init_processes() {
	first_process = 0;
}

/* creates a process, returns 0 if there was an error */
struct Process *create_process() {
	/* create a memory space for it*/
	struct Process *proc = (struct Process *)malloc(sizeof(struct Process));
	if(proc == 0) return 0; /* could not allocate it? */

	/* assign a name and pid */
	memset(proc->name, 0, 256); /* clear the name */
	last_assigned_pid++;
	proc->pid = last_assigned_pid;

	/* todo: allocate address space memory */
	proc->pml4 = create_process_address_space();
	if(proc->pml4 == 0) { /* could not allocate it? */
		free(proc);
		return 0;
	}
	proc->allocated_pages = 0;

	/* messages */
	proc->next_message = 0;
	proc->last_message = 0;
	proc->messages = 0;
	proc->waiting_thread = 0;

	/* threads */
	proc->first_thread = 0;
	proc->threads = 0;

	/* add to linked list of processes */
	if(first_process)
		first_process->previous = proc;

	proc->next = first_process;
	proc->previous = 0;
	first_process = proc;

	return proc;
}

/* dstroys a process */
void destroy_process(struct Process *process) {
	/* todo - free each thread */

	/* free each message */
	while(process->next_message != 0) {
		struct Message *next = process->next_message->next;
		release_message(process->next_message);
		process->next_message = next;
	}

	/* free the address space */
	free_process_address_space(process->pml4);

	/* free the process */
	free(process);
}

/* returns a process with that pid, returns 0 if it doesn't exist */
struct Process *get_process_from_pid(size_t pid) {
	struct Process *proc = first_process;
	while(proc != 0) {
		if(proc->pid == pid)
			return proc;

		proc = proc->next;
	}

	return 0;
}