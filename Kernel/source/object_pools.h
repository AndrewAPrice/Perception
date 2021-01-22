#pragma once

struct Message;
struct ProcessToNotifyOnExit;

// Object pools, for fast grabbing and releasing objects that are created/destoyed a lot.

// Allocate a message.
struct Message* AllocateMessage();

// Release a message.
void ReleaseMessage(struct Message* message);

// Allocate a ProcessToNotifyOnExit.
struct ProcessToNotifyOnExit* AllocateProcessToNotifyOnExit();

// Release a ProcessToNotifyOnExit.
void ReleaseProcessToNotifyOnExit(
	struct ProcessToNotifyOnExit* process_to_notify_on_exit);

// Clean up object pools to gain some memory back.
void CleanUpObjectPools();