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
	/* general info */
	char name[256]; /* name of the process */
	size_t pid;

	/* memory stuff */
	size_t pml4; /* physical address of this process's pml4*/
	size_t allocated_pages; /* number of allocated pages */

	/* linked list of messages */
	struct Message *next_message; /* start fetching messages here */
	struct Message *last_message; /* add messages here */
	unsigned short messages;
	struct Thread *waiting_thread; /* thread waiting for a message */

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

// Returns a process with the provided pid, returns 0 if it doesn't exist.
extern struct Process *GetProcessFromPid(size_t pid);
