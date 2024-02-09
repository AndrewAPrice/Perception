#include "messages.h"

#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_allocator.h"

namespace {

// The maximum number of messages that can be queued.
#define MAX_EVENTS_QUEUED 1024

// Magic number for when there are no messages queued.
#define ID_FOR_NO_EVENTS 0xFFFFFFFFFFFFFFFF

// Message status codes to send back to the sender:
#define MS_SUCCESS 0
#define MS_PROCESS_DOESNT_EXIST 1
#define MS_OUT_OF_MEMORY 2
#define MS_RECEIVERS_QUEUE_IS_FULL 3
#define MS_UNIMPLEMENTED 4

// Loads an message in to the thread.
void LoadMessageIntoThread(Message* message, Thread* thread) {
  // Set the thread's registers to contain this message.
  Registers* registers = thread->registers;
  registers->rax = message->message_id;
  registers->rbx = message->sender_pid;
  registers->rdx = message->metadata;
  registers->rsi = message->param1;
  registers->r8 = message->param2;
  registers->r9 = message->param3;
  registers->r10 = message->param4;
  registers->r12 = message->param5;

  ObjectPool<Message>::Release(message);
}

// Is this a message that involves transferring memory pages?
bool IsPagingMessage(size_t metadata) { return (metadata & 1) == 1; }

// Sends an message to a process.
void SendMessageToProcess(Message* message, Process* receiver) {
  if (receiver->thread_sleeping_for_message != nullptr) {
    // There is a thread sleeping for messages.
    if (receiver->messages_queued != 0) {
      // This should never happen.
      print << 
          "A thread is sleeping for messages even though there are messages "
          "queued.\n";
    }
    // Wake the thread that is sleeping.
    Thread* thread_to_wake = receiver->thread_sleeping_for_message;
    receiver->thread_sleeping_for_message =
        thread_to_wake->next_thread_sleeping_for_messages;

    if (!thread_to_wake->thread_is_waiting_for_message) {
      // This should never happen.
      print << "thread_is_waiting_for_message == false\n";
    }
    if (thread_to_wake->awake) {
      // This should never happen.
      print << "Thread waiting for message isn't even asleep.\n";
    }

    LoadMessageIntoThread(message, thread_to_wake);

    // Wake up the thread.
    thread_to_wake->thread_is_waiting_for_message = false;
    ScheduleThread(thread_to_wake);

    return;
  }

  if (receiver->last_message == nullptr) {
    // No messages are queued, this is the only one.
    receiver->next_message = message;
    receiver->last_message = message;
    receiver->messages_queued = 1;
  } else {
    // Add it to the end of the list of queued messages.
    receiver->last_message->next_message = message;
    receiver->last_message = message;
    receiver->messages_queued++;
  }

  // We're the last element on the list.
  message->next_message = nullptr;
}

// Can this process receive an message?
bool CanProcessReceiveMessage(Process* receiver) {
  return receiver->messages_queued < MAX_EVENTS_QUEUED;
}

}  // namespace

// Sends a message from the kernel to a process. The message will be ignored on
// an error.
void SendKernelMessageToProcess(Process* receiver_process,
                                size_t event_id, size_t param1, size_t param2,
                                size_t param3, size_t param4, size_t param5) {
  if (!CanProcessReceiveMessage(receiver_process)) {
    // The receiver's queue is full.
    return;
  }

  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) {
    // Out of memory.
    return;
  }

  // Creates the message from the parameters.
  message->message_id = event_id;
  message->sender_pid = 0;
  message->metadata = 0;
  message->param1 = param1;
  message->param2 = param2;
  message->param3 = param3;
  message->param4 = param4;
  message->param5 = param5;

  // Send the message to the receiver.
  SendMessageToProcess(message, receiver_process);
}

// Sends an message from a thread. This is intended to be called from within a
// syscall.
void SendMessageFromThreadSyscall(Thread* sender_thread) {
  Process* sender_process = sender_thread->process;
  Registers* registers = sender_thread->registers;

  // Find the receiver process, which maybe ourselves.
  Process* receiver_process = (registers->rbx == sender_process->pid)
                                         ? sender_process
                                         : GetProcessFromPid(registers->rbx);

  if (receiver_process == nullptr) {
    // Error, process doesn't exist.
    registers->rax = MS_PROCESS_DOESNT_EXIST;
    return;
  }

  if (!CanProcessReceiveMessage(receiver_process)) {
    // Error, the receiver's queue is full.
    registers->rax = MS_RECEIVERS_QUEUE_IS_FULL;
    return;
  }

  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) {
    // Error, out of memory.
    registers->rax = MS_OUT_OF_MEMORY;
    return;
  }

  // Reads the message from the registers.
  message->message_id = registers->rax;
  message->sender_pid = sender_process->pid;
  message->metadata = registers->rdx;
  message->param1 = registers->rsi;
  message->param2 = registers->r8;
  message->param3 = registers->r9;
  if (IsPagingMessage(message->metadata) &&
      receiver_process != sender_process) {
    // Transfer memory pages.
    // r10/param 4 = Address of the first memory page.
    // r12/param 5 = Size of the message in pages.

    // Figure out where to move the memory from and to.
    size_t size_in_pages = registers->r12;
    size_t source_virtual_address = registers->r10;
    size_t destination_virtual_address = FindAndReserveFreePageRange(
        &receiver_process->virtual_address_space, size_in_pages);

    if (destination_virtual_address == OUT_OF_MEMORY) {
      // Out of memory - release message and all source pages.
      ReleaseVirtualMemoryInAddressSpace(&sender_process->virtual_address_space,
                                         source_virtual_address, size_in_pages,
                                         true);
      registers->rax = MS_OUT_OF_MEMORY;
      ObjectPool<Message>::Release(message);
      return;
    }

    // Move each page over.
    for (size_t page = 0; page < size_in_pages; page++) {
      // Get the physical address of this page.
      size_t page_physical_address =
          GetPhysicalAddress(&sender_process->virtual_address_space,
                             source_virtual_address + page * PAGE_SIZE,
                             /*ignore_unownwed_pages=*/true);
      if (page_physical_address == OUT_OF_MEMORY) {
        // No memory was mapped to this area. Release message and all
        // source and destination pages.
        ReleaseVirtualMemoryInAddressSpace(
            &sender_process->virtual_address_space, source_virtual_address,
            size_in_pages, true);
        ReleaseVirtualMemoryInAddressSpace(
            &receiver_process->virtual_address_space,
            destination_virtual_address, size_in_pages, true);
        registers->rax = MS_OUT_OF_MEMORY;
        ObjectPool<Message>::Release(message);
        return;
      }

      // Unmap the physical page from the old process.
      UnmapVirtualPage(&sender_process->virtual_address_space,
                       source_virtual_address + page * PAGE_SIZE, false);

      // Map the physical page to the new process.
      MapPhysicalPageToVirtualPage(
          &receiver_process->virtual_address_space,
          destination_virtual_address + page * PAGE_SIZE, page_physical_address,
          /*own=*/true, true, false);
    }

    // Point our message to the new virtual address.
    message->param4 = destination_virtual_address;
    message->param5 = size_in_pages;
  } else {
    message->param4 = registers->r10;
    message->param5 = registers->r12;
  }

  // Send the message to the receiver.
  registers->rax = MS_SUCCESS;
  SendMessageToProcess(message, receiver_process);
}

// Gets the next message queued for a process. Returns nullptr if there are no
// messages queued.
Message* GetNextQueuedMessage(Process* receiver) {
  if (receiver->next_message == nullptr) {
    // No messages are queued.
    return nullptr;
  }

  // Grab the message at the front of the list.
  Message* message = receiver->next_message;
  receiver->next_message = message->next_message;

  if (receiver->next_message == nullptr) {
    // We removed the last item from the list.
    receiver->last_message = nullptr;
  }

  receiver->messages_queued--;
  return message;
}

// Loads the next queued message for the process into the thread.
void LoadNextMessageIntoThread(Thread* thread) {
  Message* message = GetNextQueuedMessage(thread->process);
  if (message == nullptr) {
    // There is no message queued.
    thread->registers->rax = ID_FOR_NO_EVENTS;
  } else {
    // We have an message to load.
    LoadMessageIntoThread(message, thread);
  }
}

// Sleeps a thread until an message. Returns if the thread is now asleep, or
// false if a message was loaded.
bool SleepThreadUntilMessage(Thread* thread) {
  if (!thread->awake || thread->thread_is_waiting_for_message) {
    print << "Can't sleep a thread that is already asleep.\n";
    return false;
  }

  // Check if there is an message queued.
  if (thread->process->next_message != nullptr) {
    LoadNextMessageIntoThread(thread);
    return false;
  }

  // Add to the stack of threads that are sleeping for an message.
  thread->next_thread_sleeping_for_messages =
      thread->process->thread_sleeping_for_message;
  thread->process->thread_sleeping_for_message = thread;
  thread->thread_is_waiting_for_message = true;

  // Unschedule this thread.
  UnscheduleThread(thread);
  return true;
}
