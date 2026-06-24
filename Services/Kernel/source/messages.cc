#include "messages.h"

#include "object_pool.h"
#include "physical_allocator.h"
#include "process.h"
#include "registers.h"
#include "scheduler.h"
#include "text_terminal.h"
#include "thread.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"

namespace {

// The maximum number of messages that can be queued.
#define MAX_EVENTS_QUEUED 1024

// Magic number for when there are no messages queued.
#define ID_FOR_NO_EVENTS 0xFFFFFFFFFFFFFFFF

// Loads an message in to the thread.
void LoadMessageIntoThread(Message* message, Thread* thread) {
  // Set the thread's registers to contain this message.
  Registers& registers = thread->registers;
  registers.rax = message->message_id;
  registers.rbx = message->sender_pid;
  registers.rdx = message->metadata;
  registers.rsi = message->param1;
  registers.r8 = message->param2;
  registers.r9 = message->param3;
  registers.r10 = message->param4;
  registers.r12 = message->param5;

  ObjectPool<Message>::Release(message);
}

// Sends an message to a process.
void SendMessageToProcess(Message* message, Process* receiver) {
  Thread* waiting_thread = receiver->threads_sleeping_for_message.PopFront();
  if (waiting_thread == nullptr) {
    // There are no threads waiting for a message, so queue this message.
    receiver->queued_messages.AddBack(message);
    receiver->messages_queued++;
  } else {
    // Sanity checks.
    if (receiver->messages_queued != 0) {
      print
          << "A thread is sleeping for messages even though there are messages "
             "queued.\n";
    }
    if (!waiting_thread->thread_is_waiting_for_message)
      print << "thread_is_waiting_for_message == false\n";
    if (waiting_thread->awake)
      print << "Thread waiting for message isn't even asleep.\n";

    LoadMessageIntoThread(message, waiting_thread);

    // Wake up the thread.
    waiting_thread->thread_is_waiting_for_message = false;
    ScheduleThread(waiting_thread);
  }
}

// Can this process receive an message?
bool CanProcessReceiveMessage(Process* receiver) {
  return receiver->messages_queued < MAX_EVENTS_QUEUED;
}

}  // namespace

// Sends a message from the kernel to a process. The message will be ignored on
// an error.
void SendKernelMessageToProcess(Process* receiver_process, size_t event_id,
                                size_t param1, size_t param2, size_t param3,
                                size_t param4, size_t param5) {
  // Check that the receiver's queue is not full.
  if (!CanProcessReceiveMessage(receiver_process)) return;

  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) return;

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

// Sends an RPC response from the kernel to a process.
void SendKernelRpcResponse(Process* receiver_process,
                           size_t response_message_id, size_t callee_pid,
                           size_t status) {
  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) return;

  message->message_id = response_message_id;
  message->sender_pid = callee_pid;
  message->metadata = 2;  // Message type RESPONSE (10)
  message->param1 = status;
  message->param2 = 0xFFFFFFFF;
  message->param3 = 0;
  message->param4 = 0;
  message->param5 = 0;

  SendMessageToProcess(message, receiver_process);
}

// Sends an message from a thread. This is intended to be called from within a
// syscall.
void SendMessageFromThreadSyscall(Thread* sender_thread) {
  Process* sender_process = sender_thread->process;
  Registers& registers = sender_thread->registers;

  size_t message_type = registers.rdx & 3;

  if (message_type == 2) {
    // Response to a call. Look up the message ID in
    // rpcs_waiting_on_this_process.
    size_t synthetic_response_message_id = registers.rax;
    size_t caller_pid = registers.rbx;

    RPC* candidate =
        sender_process->rpcs_waiting_on_this_process.SearchForItemEqualToValue(
            synthetic_response_message_id);
    RPC* matching_rpc = nullptr;
    if (candidate != nullptr && candidate->caller->pid == caller_pid) {
      matching_rpc = candidate;
    }

    if (matching_rpc == nullptr) {
      registers.rax = (size_t)Status::RESPONDING_TO_INVALID_RPC;
      return;
    }

    Process* receiver_process = matching_rpc->caller;
    size_t expected_message_id = matching_rpc->response_message_id;
    receiver_process->rpcs_this_process_is_waiting_on.Remove(matching_rpc);
    receiver_process->rpc_count--;
    sender_process->rpcs_waiting_on_this_process.Remove(matching_rpc);
    ObjectPool<RPC>::Release(matching_rpc);

    Message* message = ObjectPool<Message>::Allocate();
    if (message == nullptr) {
      registers.rax = (size_t)Status::OUT_OF_MEMORY;
      return;
    }

    message->message_id = expected_message_id;
    message->sender_pid = sender_process->pid;
    message->metadata = registers.rdx;
    message->param1 = registers.rsi;
    message->param2 = registers.r8;
    message->param3 = registers.r9;
    message->param4 = registers.r10;
    message->param5 = registers.r12;

    registers.rax = (size_t)Status::OK;
    SendMessageToProcess(message, receiver_process);
    return;
  }

  // Find the receiver process, which maybe ourselves.
  Process* receiver_process = (registers.rbx == sender_process->pid)
                                  ? sender_process
                                  : GetProcessFromPid(registers.rbx);

  if (receiver_process == nullptr) {
    // Error, process doesn't exist.
    registers.rax = (size_t)Status::PROCESS_DOESNT_EXIST;
    return;
  }

  if (message_type == 1 && sender_process->rpc_count >= 1024) {
    // Call that will expect a response.
    registers.rax = (size_t)Status::SENDERS_QUEUE_IS_FULL;
    return;
  }

  if (!CanProcessReceiveMessage(receiver_process)) {
    // Error, the receiver's queue is full.
    print << sender_process->name << " can't send to " << receiver_process->name
          << " because the message queue is full.\n";
    registers.rax = (size_t)Status::RECEIVERS_QUEUE_IS_FULL;
    return;
  }

  RPC* rpc = nullptr;
  if (message_type == 1) {
    rpc = ObjectPool<RPC>::Allocate();
    if (rpc == nullptr) {
      registers.rax = (size_t)Status::OUT_OF_MEMORY;
      return;
    }
  }

  Message* message = ObjectPool<Message>::Allocate();
  if (message == nullptr) {
    if (rpc != nullptr) ObjectPool<RPC>::Release(rpc);
    // Error, out of memory.
    registers.rax = (size_t)Status::OUT_OF_MEMORY;
    return;
  }

  if (rpc != nullptr) {
    rpc->caller = sender_process;
    rpc->callee = receiver_process;
    rpc->response_message_id = registers.rsi;
    rpc->synthetic_response_message_id =
        receiver_process->next_synthetic_rpc_response_message_id++;
    sender_process->rpcs_this_process_is_waiting_on.AddBack(rpc);
    sender_process->rpc_count++;
    receiver_process->rpcs_waiting_on_this_process.Insert(rpc);
  }

  // Reads the message from the registers.
  message->message_id = registers.rax;
  message->sender_pid = sender_process->pid;
  message->metadata = registers.rdx;
  message->param1 =
      rpc != nullptr ? rpc->synthetic_response_message_id : registers.rsi;
  message->param2 = registers.r8;
  message->param3 = registers.r9;
  message->param4 = registers.r10;
  message->param5 = registers.r12;

  // Send the message to the receiver.
  registers.rax = (size_t)Status::OK;
  SendMessageToProcess(message, receiver_process);
}

// Gets the next message queued for a process. Returns nullptr if there are no
// messages queued.
Message* GetNextQueuedMessage(Process* receiver) {
  Message* message = receiver->queued_messages.PopFront();
  // Check that a message is queued.
  if (message == nullptr) return nullptr;
  receiver->messages_queued--;
  return message;
}

// Loads the next queued message for the process into the thread.
void LoadNextMessageIntoThread(Thread* thread) {
  Message* message = GetNextQueuedMessage(thread->process);
  if (message == nullptr) {
    // There is no message queued.
    thread->registers.rax = ID_FOR_NO_EVENTS;
  } else {
    // There is a message to load.
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
  if (!thread->process->queued_messages.IsEmpty()) {
    LoadNextMessageIntoThread(thread);
    return false;
  }

  // Add to the stack of threads that are sleeping for an message.
  thread->process->threads_sleeping_for_message.AddBack(thread);
  thread->thread_is_waiting_for_message = true;

  // Unschedule this thread.
  UnscheduleThread(thread);
  return true;
}
