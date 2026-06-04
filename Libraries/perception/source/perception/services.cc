// Copyright 2021 Google LLC
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

#include "perception/services.h"

#include "perception/messages.h"
#include "perception/service_client.h"

namespace perception {
namespace {
constexpr int kMaxServiceNameLength = 80;
}

void RegisterService(perception::MessageId message_id, std::string_view name) {
#if defined(PERCEPTION) && !defined(TEST)
  if (name.size() > kMaxServiceNameLength) return;

  size_t service_name_words[kMaxServiceNameLength / 8];
  memset(service_name_words, 0, kMaxServiceNameLength);
  memcpy(service_name_words, &name[0], name.size());

  volatile register size_t rdi_val asm("rdi") = 32;
  volatile register size_t r15_val asm("r15") = (size_t)message_id;
  volatile register size_t rax_val asm("rax") = service_name_words[0];
  volatile register size_t rbx_val asm("rbx") = service_name_words[1];
  volatile register size_t rdx_val asm("rdx") = service_name_words[2];
  volatile register size_t rsi_val asm("rsi") = service_name_words[3];
  volatile register size_t r8_val asm("r8") = service_name_words[4];
  volatile register size_t r9_val asm("r9") = service_name_words[5];
  volatile register size_t r10_val asm("r10") = service_name_words[6];
  volatile register size_t r12_val asm("r12") = service_name_words[7];
  volatile register size_t r13_val asm("r13") = service_name_words[8];
  volatile register size_t r14_val asm("r14") = service_name_words[9];

  __asm__ __volatile__(
      "push %%rbp\n"
      "mov %%r15, %%rbp\n"
      "syscall\n"
      "pop %%rbp\n" ::"r"(rdi_val),
      "r"(r15_val), "r"(rax_val), "r"(rbx_val), "r"(rdx_val), "r"(rsi_val),
      "r"(r8_val), "r"(r9_val), "r"(r10_val), "r"(r12_val), "r"(r13_val),
      "r"(r14_val)
      : "rcx", "r11", "memory");
#endif
}

void UnregisterService(perception::MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall asm("rdi") = 33;
  volatile register size_t message_id_r asm("rax") = (size_t)message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall), "r"(message_id_r)
                       : "rcx", "r11");
#endif
}

// Finds the first service with a given name. Returns if there is at least one
// service.
bool FindFirstInstanceOfService(std::string_view name, ProcessId& process,
                                MessageId& message) {
#if defined(PERCEPTION) && !defined(TEST)
  if (name.size() >= kMaxServiceNameLength) return false;

  size_t service_name_words[kMaxServiceNameLength / 8];
  memset(service_name_words, 0, kMaxServiceNameLength);
  memcpy(service_name_words, &name[0], name.size());

  size_t number_of_services;
  size_t pid_1;
  size_t message_id_1;

  __asm__ __volatile__(
      "push %%rbx\n"
      "push %%r12\n"
      "push %%r13\n"
      "push %%r14\n"
      "push %%r15\n"

      "push %3\n"

      "mov 0(%%rsp), %%r11\n"
      "mov 0(%%r11), %%rbx\n"
      "mov 8(%%r11), %%rdx\n"
      "mov 16(%%r11), %%rsi\n"
      "mov 24(%%r11), %%r8\n"
      "mov 32(%%r11), %%r9\n"
      "mov 40(%%r11), %%r10\n"
      "mov 48(%%r11), %%r12\n"
      "mov 56(%%r11), %%r13\n"
      "mov 64(%%r11), %%r14\n"
      "mov 72(%%r11), %%r15\n"

      "add $8, %%rsp\n"

      "push %%rbp\n"

      "mov $34, %%rdi\n"
      "mov $0, %%rbp\n"
      "mov $0, %%rax\n"
      "syscall\n"

      "mov %%rbp, %%r11\n"
      "pop %%rbp\n"

      "mov %%rdi, %0\n"
      "mov %%r11, %1\n"
      "mov %%rax, %2\n"

      "pop %%r15\n"
      "pop %%r14\n"
      "pop %%r13\n"
      "pop %%r12\n"
      "pop %%rbx\n"
      : "=m"(number_of_services), "=m"(pid_1), "=m"(message_id_1)
      : "r"(service_name_words)
      : "rax", "rdi", "rdx", "rsi", "rcx", "r8", "r9", "r10", "r11", "memory");

  if (number_of_services < 1) {
    return false;
  }
  process = pid_1;
  message = message_id_1;
  return true;
#else
  return false;
#endif
}

// Calls the handler for each instance of the service.
void ForEachInstanceOfService(
    std::string_view name,
    const std::function<void(ProcessId, MessageId)>& on_each_service) {
#if defined(PERCEPTION) && !defined(TEST)
  if (name.size() >= kMaxServiceNameLength) return;

  size_t service_name_words[kMaxServiceNameLength / 8];
  memset(service_name_words, 0, kMaxServiceNameLength);
  memcpy(service_name_words, &name[0], name.size());

  size_t starting_pid = 0;
  size_t starting_message_id = 0;

  // Keep looping over all of the pages until we have a result.
  while (true) {
    volatile register size_t syscall asm("rdi") = 34;
#ifndef optimized_BUILD_
    volatile register size_t starting_pid_r asm("r11") = starting_pid;
#else
    volatile register size_t starting_pid_r asm("rbp") = starting_pid;
#endif
    volatile register size_t starting_message_id_r asm("rax") = starting_message_id;

    volatile register size_t name_1 asm("rbx") = service_name_words[0];
    volatile register size_t name_2 asm("rdx") = service_name_words[1];
    volatile register size_t name_3 asm("rsi") = service_name_words[2];
    volatile register size_t name_4 asm("r8") = service_name_words[3];
    volatile register size_t name_5 asm("r9") = service_name_words[4];
    volatile register size_t name_6 asm("r10") = service_name_words[5];
    volatile register size_t name_7 asm("r12") = service_name_words[6];
    volatile register size_t name_8 asm("r13") = service_name_words[7];
    volatile register size_t name_9 asm("r14") = service_name_words[8];
    volatile register size_t name_10 asm("r15") = service_name_words[9];

    volatile register size_t number_of_services_r asm("rdi");
#ifndef optimized_BUILD_
    volatile register size_t pid_1 asm("r11");
#else
    volatile register size_t pid_1 asm("rbp");
#endif
    volatile register size_t message_id_1 asm("rax");
    volatile register size_t pid_2 asm("rbx");
    volatile register size_t message_id_2 asm("rdx");
    volatile register size_t pid_3 asm("rsi");
    volatile register size_t message_id_3 asm("r8");
    volatile register size_t pid_4 asm("r9");
    volatile register size_t message_id_4 asm("r10");
    volatile register size_t pid_5 asm("r12");
    volatile register size_t message_id_5 asm("r13");
    volatile register size_t pid_6 asm("r14");
    volatile register size_t message_id_6 asm("r15");

    __asm__ __volatile__(
#ifndef optimized_BUILD_
        R"(
    push %%rbp
    mov %%r11, %%rbp
    syscall
    mov %%rbp, %%r11
    pop %%rbp
			)"
#else
        "syscall\n"
#endif
        : "=r"(number_of_services_r), "=r"(pid_1), "=r"(message_id_1),
          "=r"(pid_2), "=r"(message_id_2), "=r"(pid_3), "=r"(message_id_3),
          "=r"(pid_4), "=r"(message_id_4), "=r"(pid_5), "=r"(message_id_5),
          "=r"(pid_6), "=r"(message_id_6)
        : "r"(syscall), "r"(starting_pid_r), "r"(starting_message_id_r),
          "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4), "r"(name_5),
          "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9), "r"(name_10)
        : "rcx"
#ifdef optimized_BUILD_
          ,
          "r11"
#endif
    );

    size_t number_of_services = number_of_services_r;
    size_t pids[6] = {pid_1, pid_2, pid_3, pid_4, pid_5, pid_6};
    size_t message_ids[6] = {message_id_1, message_id_2, message_id_3,
                             message_id_4, message_id_5, message_id_6};

    // Call backs.
    if (number_of_services >= 1) on_each_service(pids[0], message_ids[0]);
    if (number_of_services >= 2) on_each_service(pids[1], message_ids[1]);
    if (number_of_services >= 3) on_each_service(pids[2], message_ids[2]);
    if (number_of_services >= 4) on_each_service(pids[3], message_ids[3]);
    if (number_of_services >= 5) on_each_service(pids[4], message_ids[4]);
    if (number_of_services >= 6) on_each_service(pids[5], message_ids[5]);

    // We don't have another page of results.
    if (number_of_services <= 6) return;

    // Start the next page just afer the last process ID of this page.
    starting_pid = pids[5];
    starting_message_id = message_ids[5] + 1;
  }
#endif
}

void ForEachService(
    const std::function<void(ProcessId, MessageId)>& on_each_service) {
  return ForEachInstanceOfService("", on_each_service);
}

std::string GetServiceName(ProcessId pid, MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  // Add an extra byte for the null terminator.
  char service_name[kMaxServiceNameLength + 1];
  service_name[kMaxServiceNameLength] = '\0';

  size_t was_service_found;
  size_t* name_ptr = (size_t*)service_name;

  __asm__ __volatile__(
      "push %%rbx\n"
      "push %%r12\n"
      "push %%r13\n"
      "push %%r14\n"

      "push %3\n"

      "mov %1, %%rax\n"
      "mov %2, %%rbx\n"
      "mov $47, %%rdi\n"
      "syscall\n"

      "pop %%r11\n"

      "mov %%rdi, %0\n"
      "mov %%rax, 0(%%r11)\n"
      "mov %%rbx, 8(%%r11)\n"
      "mov %%rdx, 16(%%r11)\n"
      "mov %%rsi, 24(%%r11)\n"
      "mov %%r8, 32(%%r11)\n"
      "mov %%r9, 40(%%r11)\n"
      "mov %%r10, 48(%%r11)\n"
      "mov %%r12, 56(%%r11)\n"
      "mov %%r13, 64(%%r11)\n"
      "mov %%r14, 72(%%r11)\n"

      "pop %%r14\n"
      "pop %%r13\n"
      "pop %%r12\n"
      "pop %%rbx\n"
      : "=m"(was_service_found)
      : "r"(pid), "r"(message_id), "r"(name_ptr)
      : "rax", "rdi", "rdx", "rsi", "rcx", "r8", "r9", "r10", "r11", "memory");

  if (!was_service_found) return "";

  return std::string(service_name);
#else
  return "";
#endif
}

// Calls the handler for each instance of the service that currently exists,
// and every time a new instance is registered.
MessageId NotifyOnEachNewServiceInstance(
    std::string_view name,
    const std::function<void(ProcessId, MessageId)>& on_each_service) {
#if defined(PERCEPTION) && !defined(TEST)
  if (name.size() > kMaxServiceNameLength) return 0;

  MessageId message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(
      message_id,
      [on_each_service](ProcessId sender, const MessageData& message_data) {
        // We only believe the kernel.
        if (sender != 0) return;
        on_each_service((ProcessId)message_data.param1,
                        (MessageId)message_data.param2);
      });

  size_t service_name_words[kMaxServiceNameLength / 8];
  memset(service_name_words, 0, kMaxServiceNameLength);
  memcpy(service_name_words, &name[0], name.size());

  volatile register size_t rdi_val asm("rdi") = 35;
  volatile register size_t r15_val asm("r15") = (size_t)message_id;
  volatile register size_t rax_val asm("rax") = service_name_words[0];
  volatile register size_t rbx_val asm("rbx") = service_name_words[1];
  volatile register size_t rdx_val asm("rdx") = service_name_words[2];
  volatile register size_t rsi_val asm("rsi") = service_name_words[3];
  volatile register size_t r8_val asm("r8") = service_name_words[4];
  volatile register size_t r9_val asm("r9") = service_name_words[5];
  volatile register size_t r10_val asm("r10") = service_name_words[6];
  volatile register size_t r12_val asm("r12") = service_name_words[7];
  volatile register size_t r13_val asm("r13") = service_name_words[8];
  volatile register size_t r14_val asm("r14") = service_name_words[9];

  __asm__ __volatile__(
      "push %%rbp\n"
      "mov %%r15, %%rbp\n"
      "syscall\n"
      "pop %%rbp\n" ::"r"(rdi_val),
      "r"(r15_val), "r"(rax_val), "r"(rbx_val), "r"(rdx_val), "r"(rsi_val),
      "r"(r8_val), "r"(r9_val), "r"(r10_val), "r"(r12_val), "r"(r13_val),
      "r"(r14_val)
      : "rcx", "r11", "memory");

  return message_id;
#else
  return 0;
#endif
}

// Stops calling the handler passed to NotifyOnEachService each time a new
// instance of the service is registered.
void StopNotifyingOnEachNewServiceInstance(MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  UnregisterMessageHandler(message_id);
  volatile register size_t syscall_num asm("rdi") = 36;
  volatile register size_t message_id_r asm("rax") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(message_id_r)
                       : "rcx", "r11");
#endif
}

// Calls the handler when the service disappears. The handler automatically
// unregisters if it gets triggered (although it's safe to accidentally
// call StopNotifyWhenServiceDisappears if it has triggered.)
MessageId NotifyWhenServiceDisappears(
    ProcessId process_id, MessageId message_id,
    const std::function<void()>& on_disappearance) {
#if defined(PERCEPTION) && !defined(TEST)
  MessageId notification_message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(notification_message_id,
                         [notification_message_id, on_disappearance](
                             ProcessId sender, const MessageData&) {
                           // We only believe the kernel.
                           if (sender != 0) return;
                           on_disappearance();
                           UnregisterMessageHandler(notification_message_id);
                         });

  volatile register size_t syscall_num asm("rdi") = 37;
  volatile register size_t process_id_r asm("rax") = (size_t)process_id;
  volatile register size_t message_id_r asm("rbx") = (size_t)message_id;
  volatile register size_t notification_message_id_r asm("rdx") =
      notification_message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(process_id_r),
                       "r"(message_id_r), "r"(notification_message_id_r)
                       : "rcx", "r11");
  return notification_message_id;
#else
  return 0;
#endif
}

MessageId NotifyWhenServiceDisappears(
    const ServiceClient& service_client,
    const std::function<void()>& on_disappearance) {
  return NotifyWhenServiceDisappears(service_client.ServerProcessId(),
                                     service_client.ServiceId(),
                                     on_disappearance);
}

// No longer calls the handler when the service disappears.
void StopNotifyWhenServiceDisappears(MessageId message_id) {
#if defined(PERCEPTION) && !defined(TEST)
  UnregisterMessageHandler(message_id);
  volatile register size_t syscall_num asm("rdi") = 38;
  volatile register size_t message_id_r asm("rax") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(message_id_r)
                       : "rcx", "r11");
#endif
}

}  // namespace perception
