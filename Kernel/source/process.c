#include "process.h"

#include "interrupts.h"
#include "liballoc.h"
#include "virtual_allocator.h"
#include "io.h"
#include "thread.h"

// The last assigned process ID.
size_t last_assigned_pid = 0;

//  Linked list of processes that are running.
struct Process *first_process;

// Initializes the internal structures for tracking processes.
void InitializeProcesses() {
	first_process = 0;
}

// Creates a process, returns ERROR if there was an error.
struct Process *CreateProcess(bool is_driver) {
	// Create a memory space for it.
	struct Process *proc = malloc(sizeof(struct Process));
	if(proc == 0) {
		// Out of memory.
		return (struct Process *)ERROR;
	}
	proc->is_driver = is_driver;

	// Assign a name and process ID.
	memset(proc->name, 0, 256); // Clear the name.
	last_assigned_pid++;
	proc->pid = last_assigned_pid;

	// Allocate an address space.
	proc->pml4 = CreateAddressSpace();
	if(proc->pml4 == OUT_OF_MEMORY) {
		free(proc);
		return (struct Process *)ERROR;
	}
	proc->allocated_pages = 0;

	// Messages.
	proc->next_message = NULL;
	proc->last_message = NULL;
	proc->messages_queued = 0;
	proc->thread_sleeping_for_message = NULL;
	proc->message_to_fire_on_interrupt = NULL;

	// Threads.
	proc->threads = 0;
	proc->thread_count = 0;

	// Add to the linked list of running processes.
	if(first_process) {
		first_process->previous = proc;
	}

	proc->next = first_process;
	proc->previous = 0;
	first_process = proc;

	return proc;
}

// Destroys a process .
void DestroyProcess(struct Process *process) {
	// Destroy all threads.
	DestroyThreadsForProcess(process, true);

	if (process->message_to_fire_on_interrupt != NULL) {
		UnregisterAllMessagesToForOnInterruptForProcess(process);
	}

	// Free the address space.
	FreeAddressSpace(process->pml4);

	// Free the process.
	free(process);
}

// Returns a process with the provided pid, returns NULL if it doesn't exist.
struct Process *GetProcessFromPid(size_t pid) {
	// Walk through the linked list to find our processes.
	struct Process *proc = first_process;
	while(proc != 0) {
		if(proc->pid == pid) {
			return proc;
		}

		proc = proc->next;
	}

	return (struct Process *)ERROR;
}