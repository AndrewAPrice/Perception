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
constexpr int kMaxServiceNameLength = 72;
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

  __asm__ __volatile__(
      "syscall\n" ::"r"(rdi_val),
      "r"(r15_val), "r"(rax_val), "r"(rbx_val), "r"(rdx_val), "r"(rsi_val),
      "r"(r8_val), "r"(r9_val), "r"(r10_val), "r"(r12_val), "r"(r13_val)
      : "rcx", "r11", "r14", "memory");
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

  volatile register size_t syscall asm("rdi") = 34;
  volatile register size_t starting_pid asm("rax") = 0;
  volatile register size_t starting_message_id asm("rbx") = 0;
  volatile register size_t name_1 asm("rdx") = service_name_words[0];
  volatile register size_t name_2 asm("rsi") = service_name_words[1];
  volatile register size_t name_3 asm("r8") = service_name_words[2];
  volatile register size_t name_4 asm("r9") = service_name_words[3];
  volatile register size_t name_5 asm("r10") = service_name_words[4];
  volatile register size_t name_6 asm("r12") = service_name_words[5];
  volatile register size_t name_7 asm("r13") = service_name_words[6];
  volatile register size_t name_8 asm("r14") = service_name_words[7];
  volatile register size_t name_9 asm("r15") = service_name_words[8];

  volatile register size_t number_of_services asm("rdi");
  volatile register size_t pid_1 asm("rax");
  volatile register size_t message_id_1 asm("rbx");
  volatile register size_t pid_2 asm("rdx");
  volatile register size_t message_id_2 asm("rsi");
  volatile register size_t pid_3 asm("r8");
  volatile register size_t message_id_3 asm("r9");
  volatile register size_t pid_4 asm("r10");
  volatile register size_t message_id_4 asm("r12");
  volatile register size_t pid_5 asm("r13");
  volatile register size_t message_id_5 asm("r14");

  __asm__ __volatile__(
      "syscall\n"
      : "=r"(number_of_services), "=r"(pid_1), "=r"(message_id_1),
        "=r"(pid_2), "=r"(message_id_2), "=r"(pid_3), "=r"(message_id_3),
        "=r"(pid_4), "=r"(message_id_4), "=r"(pid_5), "=r"(message_id_5)
      : "r"(syscall), "r"(starting_pid), "r"(starting_message_id),
        "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4), "r"(name_5),
        "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9)
      : "rcx", "r11"
  );

  (void)pid_2;
  (void)message_id_2;
  (void)pid_3;
  (void)message_id_3;
  (void)pid_4;
  (void)message_id_4;
  (void)pid_5;
  (void)message_id_5;

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
    volatile register size_t starting_pid_r asm("rax") = starting_pid;
    volatile register size_t starting_message_id_r asm("rbx") = starting_message_id;

    volatile register size_t name_1 asm("rdx") = service_name_words[0];
    volatile register size_t name_2 asm("rsi") = service_name_words[1];
    volatile register size_t name_3 asm("r8") = service_name_words[2];
    volatile register size_t name_4 asm("r9") = service_name_words[3];
    volatile register size_t name_5 asm("r10") = service_name_words[4];
    volatile register size_t name_6 asm("r12") = service_name_words[5];
    volatile register size_t name_7 asm("r13") = service_name_words[6];
    volatile register size_t name_8 asm("r14") = service_name_words[7];
    volatile register size_t name_9 asm("r15") = service_name_words[8];

    volatile register size_t number_of_services_r asm("rdi");
    volatile register size_t pid_1 asm("rax");
    volatile register size_t message_id_1 asm("rbx");
    volatile register size_t pid_2 asm("rdx");
    volatile register size_t message_id_2 asm("rsi");
    volatile register size_t pid_3 asm("r8");
    volatile register size_t message_id_3 asm("r9");
    volatile register size_t pid_4 asm("r10");
    volatile register size_t message_id_4 asm("r12");
    volatile register size_t pid_5 asm("r13");
    volatile register size_t message_id_5 asm("r14");

    __asm__ __volatile__(
        "syscall\n"
        : "=r"(number_of_services_r), "=r"(pid_1), "=r"(message_id_1),
          "=r"(pid_2), "=r"(message_id_2), "=r"(pid_3), "=r"(message_id_3),
          "=r"(pid_4), "=r"(message_id_4), "=r"(pid_5), "=r"(message_id_5)
        : "r"(syscall), "r"(starting_pid_r), "r"(starting_message_id_r),
          "r"(name_1), "r"(name_2), "r"(name_3), "r"(name_4), "r"(name_5),
          "r"(name_6), "r"(name_7), "r"(name_8), "r"(name_9)
        : "rcx", "r11"
    );

    size_t number_of_services = number_of_services_r;
    size_t pids[5] = {pid_1, pid_2, pid_3, pid_4, pid_5};
    size_t message_ids[5] = {message_id_1, message_id_2, message_id_3,
                             message_id_4, message_id_5};

    // Call backs.
    if (number_of_services >= 1) on_each_service(pids[0], message_ids[0]);
    if (number_of_services >= 2) on_each_service(pids[1], message_ids[1]);
    if (number_of_services >= 3) on_each_service(pids[2], message_ids[2]);
    if (number_of_services >= 4) on_each_service(pids[3], message_ids[3]);
    if (number_of_services >= 5) on_each_service(pids[4], message_ids[4]);

    // We don't have another page of results.
    if (number_of_services <= 5) return;

    // Start the next page just afer the last process ID of this page.
    starting_pid = pids[4];
    starting_message_id = message_ids[4] + 1;
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

  volatile register size_t syscall_num asm("rdi") = 47;
  volatile register size_t pid_val asm("rax") = pid;
  volatile register size_t message_id_val asm("rbx") = message_id;

  volatile register size_t was_service_found asm("rdi");
  volatile register size_t name_1 asm("rax");
  volatile register size_t name_2 asm("rbx");
  volatile register size_t name_3 asm("rdx");
  volatile register size_t name_4 asm("rsi");
  volatile register size_t name_5 asm("r8");
  volatile register size_t name_6 asm("r9");
  volatile register size_t name_7 asm("r10");
  volatile register size_t name_8 asm("r12");
  volatile register size_t name_9 asm("r13");

  __asm__ __volatile__(
      "syscall\n"
      : "=r"(was_service_found), "=r"(name_1), "=r"(name_2), "=r"(name_3),
        "=r"(name_4), "=r"(name_5), "=r"(name_6), "=r"(name_7), "=r"(name_8),
        "=r"(name_9)
      : "r"(syscall_num), "r"(pid_val), "r"(message_id_val)
      : "rcx", "r11", "r14", "r15", "memory"
  );

  if (!was_service_found) return "";

  size_t* name_ptr = (size_t*)service_name;
  name_ptr[0] = name_1;
  name_ptr[1] = name_2;
  name_ptr[2] = name_3;
  name_ptr[3] = name_4;
  name_ptr[4] = name_5;
  name_ptr[5] = name_6;
  name_ptr[6] = name_7;
  name_ptr[7] = name_8;
  name_ptr[8] = name_9;

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

  __asm__ __volatile__(
      "syscall\n" ::"r"(rdi_val),
      "r"(r15_val), "r"(rax_val), "r"(rbx_val), "r"(rdx_val), "r"(rsi_val),
      "r"(r8_val), "r"(r9_val), "r"(r10_val), "r"(r12_val), "r"(r13_val)
      : "rcx", "r11", "r14", "memory");

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
