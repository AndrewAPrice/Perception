#pragma once

struct Message;
struct ProcessToNotifyOnExit;
struct ProcessToNotifyWhenServiceAppears;
struct Service;
struct SharedMemory;
struct SharedMemoryInProcess;
struct SharedMemoryPage;
struct TimerEvent;

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


// Allocate a SharedMemory.
struct SharedMemory* AllocateSharedMemory();

// Release a SharedMemory.
void ReleaseSharedMemory(struct SharedMemory* shared_memory);

// Allocate a SharedMemoryInProcess.
struct SharedMemoryInProcess* AllocateSharedMemoryInProcess();

// Release a SharedMemoryInProcess.
void ReleaseSharedMemoryInProcess(struct SharedMemoryInProcess* shared_memory_in_process);

// Allocate a SharedMemoryPage.
struct SharedMemoryPage* AllocateSharedMemoryPage();

// Release a SharedMemoryPage.
void ReleaseSharedMemoryPage(struct SharedMemoryPage* shared_memory_page);

// Allocate a TimerEvent.
struct TimerEvent* AllocateTimerEvent();

// Release a TimerEvent.
void ReleaseTimerEvent(struct TimerEvent* timer_event);

// Initialize the object pools.
void InitializeObjectPools();

// Clean up object pools to gain some memory back.
void CleanUpObjectPools();