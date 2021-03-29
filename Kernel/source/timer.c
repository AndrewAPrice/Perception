#include "timer.h"

#include "interrupts.h"
#include "io.h"
#include "messages.h"
#include "object_pools.h"
#include "process.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "timer_event.h"

// The number of time slices (or how many times the timer triggers) per second.
#define TIME_SLICES_PER_SECOND 100
volatile size_t microseconds_since_kernel_started;
struct TimerEvent* next_scheduled_timer_event;

// Sets the timer to fire 'hz' times per second.
void SetTimerPhase(size_t hz) {
	size_t divisor = 1193180 / hz;
	outportb(0x43, 0x36);
	outportb(0x40, divisor & 0xFF);
	outportb(0x40, divisor >> 8);
}

// The function that gets called each time to timer fires.
void TimerHandler() {
	microseconds_since_kernel_started += (1000000 / TIME_SLICES_PER_SECOND);

	// Call any timer events that are scheduled to run.
	while (next_scheduled_timer_event != NULL &&
		next_scheduled_timer_event->timestamp_to_trigger_at <=
			microseconds_since_kernel_started) {
		struct TimerEvent* timer_event = next_scheduled_timer_event;

		// Remove this timer event from the front of the queue.
		next_scheduled_timer_event = timer_event->next_scheduled_timer_event;
		if(next_scheduled_timer_event != NULL)
			next_scheduled_timer_event->previous_scheduled_timer_event = NULL;

		// Remove this timer event from the process.
		if (timer_event->previous_timer_event_in_process == NULL) {
			timer_event->process_to_send_message_to->timer_event =
				timer_event->next_timer_event_in_process;
		} else {
			timer_event->previous_timer_event_in_process->
				next_timer_event_in_process =
					timer_event->next_timer_event_in_process;
		}

		if (timer_event->next_timer_event_in_process != NULL) {
			timer_event->next_timer_event_in_process->
				previous_timer_event_in_process =
					timer_event->previous_timer_event_in_process;
		}

		// Send the message to the process.
		SendKernelMessageToProcess(timer_event->process_to_send_message_to,
			timer_event->message_id_to_send, 0, 0, 0, 0, 0);

		// Release the memory for the TimerEvent.
		ReleaseTimerEvent(timer_event);
	}

	ScheduleNextThread();
}

// Initializes the timer.
void InitializeTimer() {
	microseconds_since_kernel_started = 0;
	next_scheduled_timer_event = NULL;
	SetTimerPhase(TIME_SLICES_PER_SECOND);
}


// Returns the current time, in microseconds, since the kernel has started.
size_t GetCurrentTimestampInMicroseconds() {
	return microseconds_since_kernel_started;
}

// Sends a message to the process at or after a specified number of microseconds
// have ellapsed since the kernel started.
void SendMessageToProcessAtMicroseconds(
	struct Process* process, size_t timestamp, size_t message_id) {
	struct TimerEvent* timer_event = AllocateTimerEvent();
	if (timer_event == NULL) {
		// Out of memory.
		return;
	}

	timer_event->process_to_send_message_to = process;
	timer_event->timestamp_to_trigger_at = timestamp;
	timer_event->message_id_to_send = message_id;

	// Add to global queue, in assending order based on timestamp.

	// Find the timer event we should insert ourselves after.
	struct TimerEvent* previous_timer_event = NULL;
	if (next_scheduled_timer_event != NULL &&
		next_scheduled_timer_event->timestamp_to_trigger_at < timestamp) {
		// We have events to trigger before us.
		previous_timer_event = next_scheduled_timer_event;

		// Keep iterating if there are still events to trigger before us.
		while (previous_timer_event->next_scheduled_timer_event != NULL &&
			previous_timer_event->next_scheduled_timer_event
				->timestamp_to_trigger_at < timestamp) {

			previous_timer_event = previous_timer_event->next_scheduled_timer_event;
	}
	}

	if (previous_timer_event == NULL) {
		// We're at the front of the queue!
		timer_event->previous_scheduled_timer_event = NULL;
		if (next_scheduled_timer_event != NULL) {
			next_scheduled_timer_event->previous_scheduled_timer_event = timer_event;
		}
		timer_event->next_scheduled_timer_event = next_scheduled_timer_event;
		next_scheduled_timer_event = timer_event;
	} else {
		// Insert ourselves after this timestamp.
		timer_event->previous_scheduled_timer_event =
			previous_timer_event;
		timer_event->next_scheduled_timer_event =
			previous_timer_event->next_scheduled_timer_event;

		previous_timer_event->next_scheduled_timer_event =
			timer_event;

		if (timer_event->next_scheduled_timer_event) {
			timer_event->next_scheduled_timer_event->
				previous_scheduled_timer_event = timer_event;
		}
	}

	// Add to process.
	timer_event->next_timer_event_in_process = process->timer_event;
	timer_event->previous_timer_event_in_process = NULL;
	if (process->timer_event != NULL) {
		process->timer_event->previous_timer_event_in_process =
			timer_event;
	}
	process->timer_event = timer_event;

}

// Cancel all timer events that could be scheduled for a process.
void CancelAllTimerEventsForProcess(struct Process* process) {
	while (process->timer_event != NULL) {
		struct TimerEvent* timer_event =
			process->timer_event;

		// Remove this TimerEvent from the linked list in the process.
		process->timer_event = timer_event->next_timer_event_in_process;

		// Remove from sceduled timer events.
		if (timer_event->previous_scheduled_timer_event == NULL) {
			next_scheduled_timer_event =
				timer_event->next_scheduled_timer_event;
		} else {
			timer_event->previous_scheduled_timer_event->
				next_scheduled_timer_event = 
					timer_event->next_scheduled_timer_event;
		}

		if (timer_event->next_scheduled_timer_event != NULL) {
			timer_event->next_scheduled_timer_event->
				previous_scheduled_timer_event =
					timer_event->previous_scheduled_timer_event;
		}

		// Release the memory for the TimerEvent.
		ReleaseTimerEvent(timer_event);
	}
	
}