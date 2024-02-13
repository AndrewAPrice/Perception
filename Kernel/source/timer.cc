#include "timer.h"

#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "object_pool.h"
#include "process.h"
#include "profiling.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "timer_event.h"

namespace {

// The number of time slices (or how many times the timer triggers) per second.
#define TIME_SLICES_PER_SECOND 100
volatile size_t microseconds_since_kernel_started;
TimerEvent* next_scheduled_timer_event;

#ifdef PROFILING_ENABLED
#define PROFILE_INTERVAL_IN_MICROSECONDS 10000000
size_t microseconds_until_next_profile;
#endif

// Sets the timer to fire 'hz' times per second.
void SetTimerPhase(size_t hz) {
  size_t divisor = 1193180 / hz;
  outportb(0x43, 0b00110110);
  outportb(0x40, divisor & 0xFF);
  outportb(0x40, divisor >> 8);
}

} // namespace

// The function that gets called each time to timer fires.
void TimerHandler() {
  size_t delta_time = (1000000 / TIME_SLICES_PER_SECOND);
  microseconds_since_kernel_started += delta_time;

#ifdef PROFILING_ENABLED
  if (delta_time >= microseconds_until_next_profile) {
    PrintProfilingInformation();
    microseconds_until_next_profile = PROFILE_INTERVAL_IN_MICROSECONDS;
  } else {
    microseconds_until_next_profile -= delta_time;
  }
#endif

  // Call any timer events that are scheduled to run.
  while (next_scheduled_timer_event != nullptr &&
         next_scheduled_timer_event->timestamp_to_trigger_at <=
             microseconds_since_kernel_started) {
    TimerEvent* timer_event = next_scheduled_timer_event;

    // Remove this timer event from the front of the queue.
    next_scheduled_timer_event = timer_event->next_scheduled_timer_event;
    if (next_scheduled_timer_event != nullptr)
      next_scheduled_timer_event->previous_scheduled_timer_event = nullptr;

    // Remove this timer event from the process.
    if (timer_event->previous_timer_event_in_process == nullptr) {
      timer_event->process_to_send_message_to->timer_event =
          timer_event->next_timer_event_in_process;
    } else {
      timer_event->previous_timer_event_in_process
          ->next_timer_event_in_process =
          timer_event->next_timer_event_in_process;
    }

    if (timer_event->next_timer_event_in_process != nullptr) {
      timer_event->next_timer_event_in_process
          ->previous_timer_event_in_process =
          timer_event->previous_timer_event_in_process;
    }

    // Send the message to the process.
    SendKernelMessageToProcess(timer_event->process_to_send_message_to,
                               timer_event->message_id_to_send, 0, 0, 0, 0, 0);

    // Release the memory for the TimerEvent.
    ObjectPool<TimerEvent>::Release(timer_event);
  }

  ScheduleNextThread();
}

// Initializes the timer.
void InitializeTimer() {
  microseconds_since_kernel_started = 0;
  next_scheduled_timer_event = nullptr;
  SetTimerPhase(TIME_SLICES_PER_SECOND);

#ifdef PROFILING_ENABLED
  microseconds_until_next_profile = PROFILE_INTERVAL_IN_MICROSECONDS;
#endif
}

// Returns the current time, in microseconds, since the kernel has started.
size_t GetCurrentTimestampInMicroseconds() {
  return microseconds_since_kernel_started;
}

// Sends a message to the process at or after a specified number of microseconds
// have ellapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(Process* process,
                                        size_t timestamp, size_t message_id) {
  TimerEvent* timer_event = ObjectPool<TimerEvent>::Allocate();
  if (timer_event == nullptr) {
    // Out of memory.
    return;
  }

  timer_event->process_to_send_message_to = process;
  timer_event->timestamp_to_trigger_at = timestamp;
  timer_event->message_id_to_send = message_id;

  // Add to global queue, in assending order based on timestamp.

  // Find the timer event we should insert ourselves after.
  TimerEvent* previous_timer_event = nullptr;
  if (next_scheduled_timer_event != nullptr &&
      next_scheduled_timer_event->timestamp_to_trigger_at < timestamp) {
    // We have events to trigger before us.
    previous_timer_event = next_scheduled_timer_event;

    // Keep iterating if there are still events to trigger before us.
    while (previous_timer_event->next_scheduled_timer_event != nullptr &&
           previous_timer_event->next_scheduled_timer_event
                   ->timestamp_to_trigger_at < timestamp) {
      previous_timer_event = previous_timer_event->next_scheduled_timer_event;
    }
  }

  if (previous_timer_event == nullptr) {
    // We're at the front of the queue!
    timer_event->previous_scheduled_timer_event = nullptr;
    if (next_scheduled_timer_event != nullptr) {
      next_scheduled_timer_event->previous_scheduled_timer_event = timer_event;
    }
    timer_event->next_scheduled_timer_event = next_scheduled_timer_event;
    next_scheduled_timer_event = timer_event;
  } else {
    // Insert ourselves after this timestamp.
    timer_event->previous_scheduled_timer_event = previous_timer_event;
    timer_event->next_scheduled_timer_event =
        previous_timer_event->next_scheduled_timer_event;

    previous_timer_event->next_scheduled_timer_event = timer_event;

    if (timer_event->next_scheduled_timer_event) {
      timer_event->next_scheduled_timer_event->previous_scheduled_timer_event =
          timer_event;
    }
  }

  // Add to process.
  timer_event->next_timer_event_in_process = process->timer_event;
  timer_event->previous_timer_event_in_process = nullptr;
  if (process->timer_event != nullptr) {
    process->timer_event->previous_timer_event_in_process = timer_event;
  }
  process->timer_event = timer_event;
}

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(Process* process) {
  while (process->timer_event != nullptr) {
    TimerEvent* timer_event = process->timer_event;

    // Remove this TimerEvent from the linked list in the process.
    process->timer_event = timer_event->next_timer_event_in_process;

    // Remove from sceduled timer events.
    if (timer_event->previous_scheduled_timer_event == nullptr) {
      next_scheduled_timer_event = timer_event->next_scheduled_timer_event;
    } else {
      timer_event->previous_scheduled_timer_event->next_scheduled_timer_event =
          timer_event->next_scheduled_timer_event;
    }

    if (timer_event->next_scheduled_timer_event != nullptr) {
      timer_event->next_scheduled_timer_event->previous_scheduled_timer_event =
          timer_event->previous_scheduled_timer_event;
    }

    // Release the memory for the TimerEvent.
    ObjectPool<TimerEvent>::Release(timer_event);
  }
}
