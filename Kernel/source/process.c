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
	memset(proc->name, 0, PROCESS_NAME_LENGTH); // Clear the name.
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

// Returns a process with the provided pid, returns NULL if it doesn't
// exist.
struct Process *GetProcessFromPid(size_t pid) {
	// Walk through the linked list to find our process.
	for (struct Process *proc = first_process; proc != NULL; proc = proc->next)
		if(proc->pid == pid)
			return proc;

	return (struct Process *)NULL;
}

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns NULL if no process exists
// with a pid >= pid.
struct Process *GetProcessOrNextFromPid(size_t pid) {
	// Walk through the linked list to find our process.
	struct Process *proc = first_process;
	for (struct Process *proc = first_process; proc != NULL; proc = proc->next)
		if(proc->pid >= pid)
			return proc;

	return (struct Process *)NULL;
}

// Do two process names (of length PROCESS_NAME_LENGTH) match?
bool DoProcessNamesMatch(const char* a, const char* b) {
	for (int word = 0; word < PROCESS_NAME_WORDS; word++)
		if (((size_t*)a)[word] != ((size_t*)b)[word])
			return false;

	return true;
}

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). last_process may be NULL if you want to fetch
// the first process with the name. Returns NULL if there are no more processes
// with the provided name.
struct Process* FindNextProcessWithName(const char* name,
	struct Process* start_from) {
	struct Process* potential_process = start_from;
	// Loop over every process.
	while (potential_process != NULL) {
		if (DoProcessNamesMatch(name, potential_process->name))
			// We found a process with this name!
			return potential_process;
		// Try the next process.
		potential_process = potential_process->next;
	}

	// No process was found with the name.
	return NULL;
}
