#pragma once
#include "types.h"
/*
struct reg128 {
	size_t low;
	size_t high;
};
typedef size_t reg64;*/

struct Message;
struct Thread;

struct Process {
	// General information.
	char name[256]; // Name of the process.
	size_t pid; // The process ID.

	// Memory stuff.
	size_t pml4; // The physical address of this process's pml4.
	size_t allocated_pages; // The number of allocated pages.

	// Messaging stuff.
	struct Message* next_message; // Next message on the queue.
	struct Message* last_message; // Last message on the queue.
	size_t messages_queued; // Number of messages queued.
	struct Thread *thread_sleeping_for_message; // Thread sleeping for a message.

	/* threads */
	struct Thread *threads;
	unsigned short thread_count;

	/* linked list of processes */
	struct Process *next;
	struct Process *previous;
};

// Initializes the internal structures for tracking processes.
extern void InitializeProcesses();

// Creates a process, returns ERROR if there was an error.
extern struct Process *CreateProcess();

// Destroys a process - DO NOT CALL THIS DIRECTLY, destroy a process by destroying all of it's threads!
extern void DestroyProcess(struct Process* process);

// Returns a process with the provided pid, returns NULL if it doesn't exist.
extern struct Process *GetProcessFromPid(size_t pid);
