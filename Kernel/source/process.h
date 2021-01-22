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
struct Thread;

struct ProcessToNotifyOnExit {
	// The process to trigger a message for when it dies.
	struct Process* process_listening_for;

	// The process to notify when the above process dies.
	struct Process* process_to_notify;

	// The ID of the notification message.
	size_t message_id;

	// Linked list of notification messages within `process_listening_for`.
	struct ProcessToNotifyOnExit* previous_in_process_to_notify;
	struct ProcessToNotifyOnExit* next_in_process_to_notify;

	// Linked list of notification messages within `process_to_notify`.
	struct ProcessToNotifyOnExit* previous_in_process_listening_for;
	struct ProcessToNotifyOnExit* next_in_process_listening_for;
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

	struct ProcessToNotifyOnExit* processes_to_notify_when_i_die;
	struct ProcessToNotifyOnExit* processes_i_want_to_be_notified_of_when_they_die;
};

// Initializes the internal structures for tracking processes.
extern void InitializeProcesses();

// Creates a process, returns ERROR if there was an error.
extern struct Process *CreateProcess(bool is_driver);

// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by
// destroying all of it's threads!
extern void DestroyProcess(struct Process* process);

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
