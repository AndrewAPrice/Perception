#include "timer.h"

#include "interrupts.h"
#include "io.h"
#include "linked_list.h"
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
LinkedList<TimerEvent, &TimerEvent::node_in_all_timer_events>
    scheduled_timer_events;

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

}  // namespace

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
  while (true) {
    TimerEvent* timer_event = scheduled_timer_events.FirstItem();
    if (timer_event == nullptr || timer_event->timestamp_to_trigger_at >
                                      microseconds_since_kernel_started) {
      // Timer events are sorted, so terminate early after encountering the
      // first timer event that should not yet be triggered.
      break;
    }
    scheduled_timer_events.Remove(timer_event);

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
  new (&scheduled_timer_events)
      LinkedList<TimerEvent, &TimerEvent::node_in_all_timer_events>();
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
void SendMessageToProcessAtMicroseconds(Process* process, size_t timestamp,
                                        size_t message_id) {
  TimerEvent* timer_event = ObjectPool<TimerEvent>::Allocate();
  if (timer_event == nullptr) return;

  timer_event->process_to_send_message_to = process;
  timer_event->timestamp_to_trigger_at = timestamp;
  timer_event->message_id_to_send = message_id;

  // Add to global queue, in assending order based on timestamp.
  // Find the timer event we should insert ourselves before.
  TimerEvent* next_scheduled_timer_event = scheduled_timer_events.FirstItem();
  while (next_scheduled_timer_event != nullptr &&
         next_scheduled_timer_event->timestamp_to_trigger_at < timestamp) {
    next_scheduled_timer_event =
        scheduled_timer_events.NextItem(next_scheduled_timer_event);
  }

  scheduled_timer_events.InsertBefore(next_scheduled_timer_event, timer_event);
  // Add to process.
  process->timer_events.AddBack(timer_event);
}

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(Process* process) {
  while (TimerEvent* timer_event = process->timer_events.PopFront()) {
    scheduled_timer_events.Remove(timer_event);
    ObjectPool<TimerEvent>::Release(timer_event);
  }
}
