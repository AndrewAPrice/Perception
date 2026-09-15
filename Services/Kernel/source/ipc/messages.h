#pragma once

#include "containers/linked_list.h"
#include "types.h"

namespace processes {
struct Process;
}

namespace scheduling {
struct Thread;
}  // namespace scheduling

namespace ipc {

// Type of IPC message.
enum class MessageType : size_t {
  // One way message - it doesn't expect a response.
  OneWay = 0,
  // A call that expects a response.
  Call = 1,
  // A response to a call.
  Response = 2
};

struct Message {
  // ID of the message (passed in rax.)
  size_t message_id;
  // The sender's PID (passed in r10.)
  size_t sender_pid;
  // Message metadata.
  size_t metadata;
  // Parameters:
  size_t param1;  // Passed in rsi.
  size_t param2;  // Passed in rdx.
  size_t param3;  // Passed in rbx.
  size_t param4;  // Passed in r8.
  size_t param5;  // Passed in r9.

  // The node in a queue of messages for a process.
  containers::LinkedListNode node;
};

// Sets message registers directly into the target thread.
void SetMessageInThread(scheduling::Thread& thread, size_t message_id,
                        size_t sender_pid, size_t metadata, size_t param1,
                        size_t param2, size_t param3, size_t param4,
                        size_t param5);

// Loads a queued message into thread registers and returns the message to the
// pool.
void LoadMessageIntoThread(Message* message, scheduling::Thread& thread);

// Sends a message from the kernel to a process. The message will be ignored on
// an error.
void SendKernelMessageToProcess(processes::Process* receiver_process,
                                size_t event_id, size_t param1, size_t param2,
                                size_t param3, size_t param4, size_t param5);

// Sends an RPC response from the kernel to a process.
void SendKernelRpcResponse(processes::Process* receiver_process,
                           size_t response_message_id, size_t callee_pid,
                           size_t status);

// Sends an message from a thread. This is intended to be called from within a
// syscall.
void SendMessageFromThreadSyscall(scheduling::Thread* sender_thread);

// Loads the next queued message for the process into the thread.
void LoadNextMessageIntoThread(scheduling::Thread* thread);

// Sleeps a thread until an message. Returns if the thread is now asleep, or
// false if a message was loaded.
bool SleepThreadUntilMessage(scheduling::Thread* thread);

// Gets the next message queued for a process. Returns nullptr if there are no
// messages queued.
Message* GetNextQueuedMessage(processes::Process* receiver);

}  // namespace ipc
