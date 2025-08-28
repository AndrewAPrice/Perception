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

#include "perception/interrupts.h"

#include "perception/messages.h"

namespace perception {

// Registers a handler to call upon receiving an interrupt.
MessageId RegisterInterruptHandler(uint8 interrupt,
                                   std::function<void()> handler) {
  MessageId message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(
      message_id, [handler](ProcessId pid, const MessageData &) {
        if (pid != 0) return;  // Only messages from the kernel are interrupts.
        handler();
      });
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 20;
  volatile register size_t interrupt_r asm("rax") = (size_t)interrupt;
  volatile register size_t message_id_r asm("rbx") = message_id;
  volatile register size_t method_r asm("rdx") = 0;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(interrupt_r),
                       "r"(message_id_r), "r"(method_r)
                       : "rcx", "r11");
#endif
  return message_id;
}

MessageId RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
    uint8 interrupt, uint16 status_port, uint8 mask, uint16 read_port,
    std::function<void(const uint8 *bytes)> handler) {
  MessageId message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(
      message_id, [handler](ProcessId pid, const MessageData &data) {
        if (pid != 0) return;  // Only messages from the kernel are interrupts.
        handler(data.bytes);
      });
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 20;
  volatile register size_t interrupt_r asm("rax") = (size_t)interrupt;
  volatile register size_t message_id_r asm("rbx") = message_id;
  volatile register size_t method_r asm("rdx") = 1;
  volatile register size_t params_r asm("rsi") =
      static_cast<uint64>(status_port) |
      (static_cast<uint64>(read_port) << 16) | (static_cast<uint64>(mask) << 32);

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(interrupt_r),
                       "r"(message_id_r), "r"(method_r), "r"(params_r)
                       : "rcx", "r11");
#endif

  return message_id;
}

// Unregisters a handler to call upon receiving an interrupt
void UnegisterInterruptHandler(uint8 interrupt, MessageId message_id) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 21;
  volatile register size_t interrupt_r asm("rax") = (size_t)interrupt;
  volatile register size_t message_id_r asm("rbx") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(interrupt_r),
                       "r"(message_id_r)
                       : "rcx", "r11");
#endif
  UnregisterMessageHandler(message_id);
}

}  // namespace perception
