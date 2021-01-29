#pragma once

struct Message;
struct ProcessToNotifyOnExit;
struct ProcessToNotifyWhenServiceAppears;
struct Service;

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

// Allocate a Service.
struct Service* AllocateService();

// Release a Service.
void ReleaseService(struct Service* service);

// Allocate a ProcessToNotifyWhenServiceAppears.
struct ProcessToNotifyWhenServiceAppears* AllocateProcessToNotifyWhenServiceAppears();

// Release a ProcessToNotifyWhenServiceAppears.
void ReleaseProcessToNotifyWhenServiceAppears(
	struct ProcessToNotifyWhenServiceAppears* process_to_notify_when_service_appears);

// Clean up object pools to gain some memory back.
void CleanUpObjectPools();