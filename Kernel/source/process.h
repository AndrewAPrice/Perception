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
	// Unique ID to identify this process.
	size_t pid;

	// Name of the process.
	char name[256];

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

	// Linked list of threads.
	struct Thread *threads;
	// Number of threads this process has..
	unsigned short thread_count;

	// Linked list of processes.
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
