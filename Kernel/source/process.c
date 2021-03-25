#include "process.h"

#include "interrupts.h"
#include "io.h"
#include "liballoc.h"
#include "messages.h"
#include "object_pools.h"
#include "service.h"
#include "thread.h"
#include "timer.h"
#include "virtual_allocator.h"

// The last assigned process ID.
size_t last_assigned_pid;

//  Linked list of processes that are running.
struct Process *first_process;
struct Process *last_process;

// Initializes the internal structures for tracking processes.
void InitializeProcesses() {
	last_assigned_pid = 0;
	first_process = NULL;
	last_process = NULL;
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


	// Various linked lists of that should be initialized to NULL.
	proc->next_message = NULL;
	proc->last_message = NULL;
	proc->messages_queued = 0;
	proc->thread_sleeping_for_message = NULL;
	proc->message_to_fire_on_interrupt = NULL;
	proc->processes_to_notify_when_i_die = NULL;
	proc->processes_i_want_to_be_notified_of_when_they_die = NULL;
	proc->services_i_want_to_be_notified_of_when_they_appear = NULL;
	proc->first_service = NULL;
	proc->last_service = NULL;
	proc->shared_memory = NULL;
	proc->timer_event = NULL;

	// Threads.
	proc->threads = 0;
	proc->thread_count = 0;


	// Add to the linked list of running processes.
	if(first_process == NULL) {
		// No running processes.
		first_process = proc;
		last_process = proc;
		proc->previous = NULL;
	} else {
		last_process->next = proc;
		proc->previous = last_process;
		last_process = proc;
	}

	proc->next = NULL;

	return proc;
}

// Releases a ProcessToNotifyOnExit object and disconnects it from the
// linked lists.
void ReleaseNotification(struct ProcessToNotifyOnExit* notification) {
	// Remove from target.
	if (notification->previous_in_target == NULL) {
		notification->target->processes_to_notify_when_i_die =
			notification->next_in_target;
	} else {
		notification->previous_in_target->next_in_target =
			notification->next_in_target;
	}

	if (notification->next_in_target != NULL) {
		notification->next_in_target->previous_in_target =
			notification->previous_in_target;
	}

	// Remove from notifyee.
	if (notification->previous_in_notifyee == NULL) {
		notification->notifyee->processes_i_want_to_be_notified_of_when_they_die =
			notification->next_in_notifyee;
	} else {
		notification->previous_in_notifyee->next_in_notifyee =
			notification->next_in_notifyee;
	}

	if (notification->next_in_notifyee != NULL) {
		notification->next_in_notifyee->previous_in_notifyee =
			notification->previous_in_notifyee;
	}

	ReleaseProcessToNotifyOnExit(notification);
}

// Destroys a process .
void DestroyProcess(struct Process *process) {
	// Destroy all threads.
	DestroyThreadsForProcess(process, true);

	if (process->message_to_fire_on_interrupt != NULL)
		UnregisterAllMessagesToForOnInterruptForProcess(process);

	while (process->services_i_want_to_be_notified_of_when_they_appear != NULL)
		StopNotifyingProcessWhenServiceAppears(
			process->services_i_want_to_be_notified_of_when_they_appear);

	while (process->first_service != NULL)
		UnregisterService(process->first_service);

	if (process->timer_event != NULL)
		CancelAllTimerEventsForProcess(process);

	// Release any shared memory mapped into this process.
	while (process->shared_memory != NULL) 
		UnmapSharedMemoryFromProcess(process, process->shared_memory);

	// Free the address space.
	FreeAddressSpace(process->pml4);

	// Free all notifications I was waiting on for processes to die.
	while (process->processes_i_want_to_be_notified_of_when_they_die
		!= NULL) {
		ReleaseNotification(
			process->processes_i_want_to_be_notified_of_when_they_die);
	}

	// Notify the processes that were wanting to know when this process died.
	while (process->processes_to_notify_when_i_die != NULL) {
		SendKernelMessageToProcess(
			process->processes_to_notify_when_i_die->notifyee,
			process->processes_to_notify_when_i_die->event_id,
			process->pid, 0, 0, 0, 0);
		ReleaseNotification(process->processes_to_notify_when_i_die);
	}

	// Remove from linked list.
	if (process->previous == NULL) {
		first_process = process->next;
	} else {
		process->previous->next = process->next;
	}

	if (process->next == NULL) {
		last_process = process->previous;
	} else {
		process->next->previous = process->previous;
	}

	// Free the process.
	free(process);
}

// Registers that a process wants to be notified if another process dies.
extern void NotifyProcessOnDeath(struct Process* target,
	struct Process* notifyee, size_t event_id) {
	struct ProcessToNotifyOnExit* notification = AllocateProcessToNotifyOnExit();
	if (notification == NULL)
		return;

	notification->target = target;
	notification->notifyee = notifyee;
	notification->event_id = event_id;

	notification->previous_in_target = NULL;
	notification->next_in_target = target->processes_to_notify_when_i_die;
	target->processes_to_notify_when_i_die = notification;

	notification->previous_in_notifyee = NULL;
	notification->next_in_notifyee =
		notifyee->processes_i_want_to_be_notified_of_when_they_die;
	notifyee->processes_i_want_to_be_notified_of_when_they_die =
		notification;
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
	for (struct Process *proc = first_process; proc != NULL; proc = proc->next) {
		if(proc->pid >= pid)
			return proc;
	}

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
