// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "linux_syscalls/poll.h"

#include <errno.h>
#include <poll.h>

#include <chrono>
#include <vector>

#include "files.h"
#include "perception/messages.h"
#include "perception/time.h"

namespace perception {
namespace linux_syscalls {
namespace {

// System call number for scheduling a timer message after a duration.
constexpr size_t kSendMessageAfterMicrosecondsSyscall = 23;

struct RegisteredPipeWaiter {
  std::shared_ptr<SharedMemoryPipe> pipe;
  bool is_writer;
};

void SendMessageAfterXMicroseconds(size_t microseconds, MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") =
      kSendMessageAfterMicrosecondsSyscall;
  volatile register size_t microseconds_r asm("rax") = microseconds;
  volatile register size_t message_id_r asm("rbx") = message_id;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall), "r"(microseconds_r), "r"(message_id_r)
                       : "rcx", "r11");
#else
  (void)microseconds;
  (void)message_id;
#endif
}

long ScanPollFds(struct pollfd* fds, nfds_t nfds) {
  long ready_count = 0;
  for (nfds_t i = 0; i < nfds; i++) {
    auto& pfd = fds[i];
    pfd.revents = 0;
    if (pfd.fd < 0)
      continue;

    auto desc = GetFileDescriptor(pfd.fd);
    if (!desc) {
      if (pfd.fd == 0) {
        if (pfd.events & (POLLIN | POLLRDNORM))
          pfd.revents |= POLLHUP;
      } else if (pfd.fd == 1 || pfd.fd == 2) {
        if (pfd.events & (POLLOUT | POLLWRNORM))
          pfd.revents |= (pfd.events & (POLLOUT | POLLWRNORM));
      } else {
        pfd.revents = POLLNVAL;
      }
      if (pfd.revents != 0)
        ready_count++;
      continue;
    }

    switch (desc->type) {
      case FileDescriptor::PIPE: {
        if (!desc->pipe.pipe) {
          pfd.revents = POLLNVAL;
          break;
        }
        if (!desc->pipe.is_writer) {
          if ((pfd.events & (POLLIN | POLLRDNORM)) &&
              desc->pipe.pipe->BufferedBytes() > 0) {
            pfd.revents |= (pfd.events & (POLLIN | POLLRDNORM));
          }
          if (desc->pipe.pipe->IsEof())
            pfd.revents |= POLLHUP;
        } else {
          if (desc->pipe.pipe->IsBrokenPipe()) {
            pfd.revents |= POLLERR;
          } else if ((pfd.events & (POLLOUT | POLLWRNORM)) &&
                     desc->pipe.pipe->FreeSpace() > 0) {
            pfd.revents |= (pfd.events & (POLLOUT | POLLWRNORM));
          }
        }
        break;
      }
      case FileDescriptor::FILE:
      case FileDescriptor::DIRECTORY: {
        if (pfd.events & (POLLIN | POLLRDNORM))
          pfd.revents |= (pfd.events & (POLLIN | POLLRDNORM));
        if (pfd.events & (POLLOUT | POLLWRNORM))
          pfd.revents |= (pfd.events & (POLLOUT | POLLWRNORM));
        break;
      }
      case FileDescriptor::SOCKET: {
        if (pfd.events & (POLLOUT | POLLWRNORM))
          pfd.revents |= (pfd.events & (POLLOUT | POLLWRNORM));
        break;
      }
    }

    if (pfd.revents != 0)
      ready_count++;
  }
  return ready_count;
}

void UnregisterAllWaiters(const std::vector<RegisteredPipeWaiter>& waiters,
                          MessageId message_id) {
  for (const auto& waiter : waiters)
    waiter.pipe->UnregisterWaiter(waiter.is_writer, message_id);
  UnregisterMessageHandler(message_id);
}

}  // namespace

long poll(struct pollfd* fds, nfds_t nfds, int timeout) {
  if (!fds && nfds > 0) {
    errno = EFAULT;
    return -EFAULT;
  }

  long ready_count = ScanPollFds(fds, nfds);
  if (ready_count > 0 || timeout == 0)
    return ready_count;

  std::chrono::microseconds deadline(0);
  if (timeout > 0) {
    deadline =
        GetTimeSinceKernelStarted() + std::chrono::milliseconds(timeout);
  }

  while (true) {
    MessageId wakeup_message_id = GenerateUniqueMessageId();
    RegisterWakeUpHandler(wakeup_message_id);

    std::vector<RegisteredPipeWaiter> waiters;
    for (nfds_t i = 0; i < nfds; i++) {
      if (fds[i].fd < 0)
        continue;
      auto desc = GetFileDescriptor(fds[i].fd);
      if (!desc || desc->type != FileDescriptor::PIPE || !desc->pipe.pipe)
        continue;
      desc->pipe.pipe->RegisterWaiter(desc->pipe.is_writer, wakeup_message_id);
      waiters.push_back({desc->pipe.pipe, desc->pipe.is_writer});
    }

    ready_count = ScanPollFds(fds, nfds);
    if (ready_count > 0) {
      UnregisterAllWaiters(waiters, wakeup_message_id);
      return ready_count;
    }

    if (timeout > 0) {
      auto now = GetTimeSinceKernelStarted();
      if (now >= deadline) {
        UnregisterAllWaiters(waiters, wakeup_message_id);
        return ScanPollFds(fds, nfds);
      }
      SendMessageAfterXMicroseconds(
          static_cast<size_t>((deadline - now).count()), wakeup_message_id);
    }

    ProcessId sender;
    MessageData message_data;
    SleepAndGetRawMessage(wakeup_message_id, sender, message_data);
    UnregisterAllWaiters(waiters, wakeup_message_id);

    ready_count = ScanPollFds(fds, nfds);
    if (ready_count > 0)
      return ready_count;

    if (timeout > 0 && GetTimeSinceKernelStarted() >= deadline)
      return 0;
  }
}

}  // namespace linux_syscalls
}  // namespace perception
