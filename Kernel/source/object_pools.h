#pragma once

struct Message;

// Object pools, for fast grabbing and releasing objects that are created/destoyed a lot.

// Allocate a message.
struct Message* AllocateMessage();

// Release a message.
void ReleaseMessage(struct Message* message);

// Clean up object pools to gain some memory back.
void CleanUpObjectPools();