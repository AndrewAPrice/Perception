#pragma once

#include "types.h"

struct Message {
	// ID of the message (passed in rax.)
	size_t message_id;
	// The sender's PID (passed in r10.)
	size_t sender_pid;
	// Message metadata.
	size_t metadata;
	// Parameters:
	size_t param1; // Passed in rsi.
	size_t param2; // Passed in rdx.
	size_t param3; // Passed in rbx.
	size_t param4; // Passed in r8.
	size_t param5; // Passed in r9.

	struct Message* next_message; // The next queued message for a process.
};

struct Process;
struct Thread;

// Sends a message from the kernel to a process. The message will be ignored on an error.
extern void SendKernelMessageToProcess(struct Process* receiver_process, size_t event_id,
	size_t param1, size_t param2, size_t param3, size_t param4, size_t param5);

// Sends an message from a thread. This is intended to be called from within a syscall.
extern void SendMessageFromThreadSyscall(struct Thread* sender_thread);

// Loads the next queued message for the process into the thread.
extern void LoadNextMessageIntoThread(struct Thread* thread);

// Sleeps a thread until an message. Returns if the thread is now asleep, or false
// if a message was loaded.
extern bool SleepThreadUntilMessage(struct Thread* thread);
