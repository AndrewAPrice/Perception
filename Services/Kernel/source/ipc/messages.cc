#include "ipc/messages.h"

#include "../../../Libraries/perception/public/status.h"
#include "containers/object_pool.h"
#include "containers/spinlock.h"
#include "hardware/registers.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#include "processes/process.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"

namespace ipc {

using containers::InterruptSafeSpinlockGuard;
using containers::ObjectPool;
using containers::ScopedTwoLocks;
using hardware::Registers;
using processes::GetProcessFromPid;
using processes::Process;
using processes::ProcessRef;
using scheduling::ScheduleThread;
using scheduling::ScheduleThreadDirectSwitch;
using scheduling::Thread;

namespace {

// The maximum number of messages that can be queued for a process.
constexpr size_t kMaxEventsQueued = 1024;

// Magic number indicating there are no messages queued.
constexpr size_t kIdForNoEvents = 0xFFFFFFFFFFFFFFFF;

// Maximum number of RPCs waiting per process.
constexpr size_t kMaxRpcsWaiting = 1024;

// Mask to extract the message type from message metadata.
constexpr size_t kMessageTypeMask = 3;

}  // namespace

void SetMessageInThread(Thread& thread, size_t message_id, size_t sender_pid,
                        size_t metadata, size_t param1, size_t param2,
                        size_t param3, size_t param4, size_t param5) {
  Registers& registers = thread.registers;
  registers.rax = message_id;
  registers.rbx = sender_pid;
  registers.rdx = metadata;
  registers.rsi = param1;
  registers.r8 = param2;
  registers.r9 = param3;
  registers.r10 = param4;
  registers.r12 = param5;
}

void LoadMessageIntoThread(Message* message, Thread& thread) {
  SetMessageInThread(thread, message->message_id, message->sender_pid,
                     message->metadata, message->param1, message->param2,
                     message->param3, message->param4, message->param5);
  ObjectPool<Message>::Release(message);
}

namespace {

Status DeliverOrQueueMessageLocked(Process& receiver, size_t message_id,
                                   size_t sender_pid, size_t metadata,
                                   size_t param1, size_t param2, size_t param3,
                                   size_t param4, size_t param5,
                                   Thread*& waiting_thread) {
  return receiver.message_queue.DeliverOrQueueLocked(
      message_id, sender_pid, metadata, param1, param2, param3, param4, param5,
      waiting_thread);
}

}  // namespace

// Sends a message from the kernel to a process. The message will be ignored on
// an error.
void SendKernelMessageToProcess(Process* receiver_process, size_t event_id,
                                size_t param1, size_t param2, size_t param3,
                                size_t param4, size_t param5) {
  if (receiver_process == nullptr) return;

  Thread* waiting_thread = nullptr;
  {
    InterruptSafeSpinlockGuard guard(receiver_process->message_lock);
    DeliverOrQueueMessageLocked(*receiver_process, event_id, /*sender_pid=*/0,
                                /*metadata=*/static_cast<size_t>(MessageType::OneWay),
                                param1, param2, param3, param4,
                                param5, waiting_thread);
    if (waiting_thread != nullptr)
      scheduling::AcquireThreadReference(*waiting_thread);
  }
  if (waiting_thread != nullptr) {
    ScheduleThread(waiting_thread);
    scheduling::ReleaseThreadReference(*waiting_thread);
  }
}

// Sends an RPC response from the kernel to a process.
void SendKernelRpcResponse(Process* receiver_process,
                           size_t response_message_id, size_t callee_pid,
                           size_t status) {
  if (receiver_process == nullptr) return;

  Thread* waiting_thread = nullptr;
  {
    InterruptSafeSpinlockGuard guard(receiver_process->message_lock);
    DeliverOrQueueMessageLocked(
        *receiver_process, response_message_id, callee_pid,
        /*metadata=*/static_cast<size_t>(MessageType::Response),
        /*param1=*/status, /*param2=*/0xFFFFFFFF, /*param3=*/0, /*param4=*/0,
        /*param5=*/0, waiting_thread);
    if (waiting_thread != nullptr)
      scheduling::AcquireThreadReference(*waiting_thread);
  }
  if (waiting_thread != nullptr) {
    ScheduleThread(waiting_thread);
    scheduling::ReleaseThreadReference(*waiting_thread);
  }
}


// Sends an message from a thread. This is intended to be called from within a
// syscall.
void SendMessageFromThreadSyscall(Thread* sender_thread) {
  Process* sender_process = sender_thread->process;
  Registers& registers = sender_thread->registers;

  MessageType message_type =
      static_cast<MessageType>(registers.rdx & kMessageTypeMask);

  if (message_type == MessageType::Response) {
    // Response to a call. Look up the message ID in
    // rpcs_waiting_on_this_process.
    size_t synthetic_response_message_id = registers.rax;
    size_t caller_pid = registers.rbx;

    ProcessRef receiver_ref;
    Process* receiver_process = nullptr;
    if (caller_pid == sender_process->pid) {
      receiver_process = sender_process;
    } else {
      receiver_ref = GetProcessFromPid(caller_pid);
      receiver_process = receiver_ref.get();
    }
    if (receiver_process == nullptr) {
      registers.rax = (size_t)Status::RESPONDING_TO_INVALID_RPC;
      return;
    }

    Thread* waiting_thread = nullptr;
    {
      ScopedTwoLocks locks(&sender_process->message_lock,
                           &receiver_process->message_lock);

      RPC* candidate =
          sender_process->rpcs_waiting_on_this_process
              .SearchForItemEqualToValue(synthetic_response_message_id);
      RPC* matching_rpc = nullptr;
      if (candidate != nullptr && candidate->caller->pid == caller_pid)
        matching_rpc = candidate;

      if (matching_rpc == nullptr) {
        registers.rax = (size_t)Status::RESPONDING_TO_INVALID_RPC;
        return;
      }

      size_t expected_message_id = matching_rpc->response_message_id;
      receiver_process->rpcs_this_process_is_waiting_on.Remove(matching_rpc);
      receiver_process->rpc_count--;
      sender_process->rpcs_waiting_on_this_process.Remove(matching_rpc);
      ObjectPool<RPC>::Release(matching_rpc);

      Status status = DeliverOrQueueMessageLocked(
          *receiver_process, expected_message_id, sender_process->pid,
          registers.rdx, registers.rsi, registers.r8, registers.r9,
          registers.r10, registers.r12, waiting_thread);
      registers.rax = (size_t)status;
      if (waiting_thread != nullptr)
        scheduling::AcquireThreadReference(*waiting_thread);
    }
    if (waiting_thread != nullptr) {
      ScheduleThreadDirectSwitch(waiting_thread);
      scheduling::ReleaseThreadReference(*waiting_thread);
    }
    return;
  }

  // Find the receiver process, which maybe ourselves.
  ProcessRef receiver_ref;
  Process* receiver_process = nullptr;
  if (registers.rbx == sender_process->pid) {
    receiver_process = sender_process;
  } else {
    receiver_ref = GetProcessFromPid(registers.rbx);
    receiver_process = receiver_ref.get();
  }

  if (receiver_process == nullptr) {
    // Error, process doesn't exist.
    registers.rax = (size_t)Status::PROCESS_DOESNT_EXIST;
    return;
  }


  if (message_type == MessageType::Call) {
    Thread* waiting_thread = nullptr;
    {
      ScopedTwoLocks locks(&sender_process->message_lock,
                           &receiver_process->message_lock);

      if (sender_process->rpc_count >= kMaxRpcsWaiting) {
        // Call that will expect a response.
        registers.rax = (size_t)Status::SENDERS_QUEUE_IS_FULL;
        return;
      }

      RPC* rpc = ObjectPool<RPC>::Allocate();
      if (rpc == nullptr) {
        registers.rax = (size_t)Status::OUT_OF_MEMORY;
        return;
      }

      rpc->caller = sender_process;
      rpc->callee = receiver_process;
      rpc->response_message_id = registers.rsi;
      rpc->synthetic_response_message_id =
          receiver_process->next_synthetic_rpc_response_message_id++;
      sender_process->rpcs_this_process_is_waiting_on.AddBack(rpc);
      sender_process->rpc_count++;
      receiver_process->rpcs_waiting_on_this_process.Insert(rpc);

      Status status = DeliverOrQueueMessageLocked(
          *receiver_process, registers.rax, sender_process->pid, registers.rdx,
          rpc->synthetic_response_message_id, registers.r8, registers.r9,
          registers.r10, registers.r12, waiting_thread);
      if (status != Status::OK) {
        sender_process->rpcs_this_process_is_waiting_on.Remove(rpc);
        sender_process->rpc_count--;
        receiver_process->rpcs_waiting_on_this_process.Remove(rpc);
        ObjectPool<RPC>::Release(rpc);
        registers.rax = (size_t)status;
        return;
      }

      registers.rax = (size_t)Status::OK;
      if (waiting_thread != nullptr)
        scheduling::AcquireThreadReference(*waiting_thread);
    }
    if (waiting_thread != nullptr) {
      ScheduleThreadDirectSwitch(waiting_thread);
      scheduling::ReleaseThreadReference(*waiting_thread);
    }
    return;
  }

  // Normal send (message_type == MessageType::OneWay):
  Thread* waiting_thread = nullptr;
  {
    InterruptSafeSpinlockGuard guard(receiver_process->message_lock);
    Status status = DeliverOrQueueMessageLocked(
        *receiver_process, registers.rax, sender_process->pid, registers.rdx,
        registers.rsi, registers.r8, registers.r9, registers.r10, registers.r12,
        waiting_thread);
    registers.rax = (size_t)status;
    if (waiting_thread != nullptr)
      scheduling::AcquireThreadReference(*waiting_thread);
  }
  if (waiting_thread != nullptr) {
    ScheduleThread(waiting_thread);
    scheduling::ReleaseThreadReference(*waiting_thread);
  }

}

// Gets the next message queued for a process. Returns nullptr if there are no
// messages queued.
Message* GetNextQueuedMessage(Process* receiver) {
  InterruptSafeSpinlockGuard guard(receiver->message_lock);
  return receiver->message_queue.PopNextLocked();
}

// Loads the next queued message for the process into the thread.
void LoadNextMessageIntoThread(Thread* thread) {
  Message* message = GetNextQueuedMessage(thread->process);
  if (message == nullptr) {
    thread->registers.rax = kIdForNoEvents;
  } else {
    LoadMessageIntoThread(message, *thread);
  }
}

// Sleeps a thread until an message. Returns if the thread is now asleep, or
// false if a message was loaded.
bool SleepThreadUntilMessage(Thread* thread) {
  return thread->process->message_queue.PopIntoThreadOrSleep(*thread);
}

}  // namespace ipc
