#pragma once
#include "types.h"

struct Thread;
struct Registers;
struct Process;

enum class ThreadPriority : uint8 {
  InterruptDriver = 0,  // Hardware drivers
  RealtimeService = 1,  // UI Compositor / Window Manager
  InteractiveApp = 2,   // Focused GUI App (foreground)
  Normal = 3,           // Background services (Net Manager, etc.)
  Background = 4,       // CPU-heavy processes (compilers, etc.)
  Idle = 5              // Strictly idle tasks (backups, etc.)
};

constexpr int kThreadPriorityCount = 6;

// The currently running thread.
extern Thread *running_thread;

// Currently executing registers.
extern Registers *currently_executing_thread_regs;

// Initializes the scheduler.
void InitializeScheduler();

// Schedule the next thread, called from the timer inerrupt.
void ScheduleNextThread();

void ScheduleThread(Thread *thread);
void UnscheduleThread(Thread *thread);

// Set a thread's base priority and reschedule it if awake.
void SetThreadPriority(Thread* thread, ThreadPriority priority);

// Focused process elevation interface for Window Manager.
void SetFocusedProcess(Process* process);
Process* GetFocusedProcess();
bool HasAwakeThreads();

// Returns whether the running thread needs a timeslice interrupt.
bool NeedsTimesliceInterrupt(Thread* thread);

// Schedules a thread if we are currently halted - such as an interrupt
// woke up a thread.
void ScheduleThreadIfWeAreHalted();