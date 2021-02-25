#pragma once
#include "types.h"
/*
struct reg128 {
	size_t low;
	size_t high;
};
typedef size_t reg64;*/

#define PROCESS_NAME_WORDS 11
#define PROCESS_NAME_LENGTH (PROCESS_NAME_WORDS * 8)

struct MessageToFireOnInterrupt;
struct Message;
struct Process;
struct ProcessToNotifyWhenServiceAppears;
struct Service;
struct SharedMemoryInProcess;
struct Thread;

struct ProcessToNotifyOnExit {
	// The process to trigger a message for when it dies.
	struct Process* target;

	// The process to notify when the above process dies.
	struct Process* notifyee;

	// The ID of the notification message to send to notifyee.
	size_t event_id;

	// Linked list of notification messages within the targer process.
	struct ProcessToNotifyOnExit* previous_in_target;
	struct ProcessToNotifyOnExit* next_in_target;

	// Linked list of notification messages within the notifyee process.
	struct ProcessToNotifyOnExit* previous_in_notifyee;
	struct ProcessToNotifyOnExit* next_in_notifyee;
};

struct Process {
	// Unique ID to identify this process.
	size_t pid;

	// Name of the process.
	char name[PROCESS_NAME_LENGTH];

	// Is this a process a driver? Drivers have permission to do IO.
	bool is_driver;

	// The physical address of this process's pml4. This represents the virtual
	// address space that is unique to this process.
	size_t pml4;
	// The number of allocated pages.
	size_t allocated_pages; // The number of allocated pages.

	// Linked list of messages waiting send to this process, waiting to be consumed.
	struct Message* next_message;
	struct Message* last_message;
	// Number of messages queued.
	size_t messages_queued;
	// Linked queue of threads that are currently sleeping and waiting for a message.
	struct Thread *thread_sleeping_for_message;

	// Linked list of messages to fire on an interrupt.
	struct MessageToFireOnInterrupt* message_to_fire_on_interrupt;

	// Linked list of threads.
	struct Thread *threads;
	// Number of threads this process has..
	unsigned short thread_count;

	// Linked list of processes.
	struct Process *next;
	struct Process *previous;

	// Linked lists of processes to notify when I die.
	struct ProcessToNotifyOnExit* processes_to_notify_when_i_die;
	// Linked lists of processes I want to be notified of when they die.
	struct ProcessToNotifyOnExit* processes_i_want_to_be_notified_of_when_they_die;
	// Linked list of services I want to be notified of when they appear.
	struct ProcessToNotifyWhenServiceAppears* services_i_want_to_be_notified_of_when_they_appear;

	// Linked list of services in this process. System calls that scan for services
	// expect that services are added to the back of the list, and we must iterate
	// them from front to back.
	struct Service* first_service;
	struct Service* last_service;

	// Linked list of shared memory mapped into this process.
	struct SharedMemoryInProcess* shared_memory;
};

// Initializes the internal structures for tracking processes.
extern void InitializeProcesses();

// Creates a process, returns ERROR if there was an error.
extern struct Process *CreateProcess(bool is_driver);

// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by
// destroying all of it's threads!
extern void DestroyProcess(struct Process* process);

// Registers that a process wants to be notified if another process dies.
extern void NotifyProcessOnDeath(struct Process* target,
	struct Process* notifyee, size_t event_id);

// Returns a process with the provided pid, returns NULL if it doesn't exist.
extern struct Process *GetProcessFromPid(size_t pid);

// Returns a process with the provided pid, and if it doesn't exist, returns
// the process with the next highest pid. Returns NULL if no process exists
// with a pid >= pid.
extern struct Process *GetProcessOrNextFromPid(size_t pid);

// Returns the next process with the given name (which must be an array of
// length PROCESS_NAME_LENGTH). Returns NULL if there are no more processes
// with the provided name. `start_from` is inclusive.
extern struct Process* FindNextProcessWithName(const char* name,
	struct Process* start_from);
