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

#include "perception/processes.h"

#include <functional>
#include <iostream>

#include "perception/messages.h"
#ifndef PERCEPTION
#include <sched.h>
#include <stdlib.h>

#include <sstream>
#include <thread>
#endif

namespace perception {
namespace {

#ifdef PERCEPTION
ProcessId InvokeSyscallToGetProcessId() {
  volatile register size_t syscall_num asm("rdi") = 39;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num)
                       : "rcx", "r11");
  return return_val;
}
#endif

}  // namespace

ProcessId GetProcessId() {
#ifdef PERCEPTION
  return InvokeSyscallToGetProcessId();
#else
  return 0;
#endif
}

void TerminateProcess() {
#ifdef PERCEPTION
  register size_t syscall_num asm("rdi") = 6;
  __asm__("syscall\n" ::"r"(syscall_num) : "rcx", "r11");
#else
  exit(0);
#endif
}

// Terminates a process.
void TerminateProcesss(ProcessId pid) {
#ifdef PERCEPTION
  register size_t syscall_num asm("rdi") = 7;
  register size_t pid_r asm("rax") = pid;
  __asm__("syscall\n" ::"r"(syscall_num), "r"(pid_r) : "rcx", "r11");
#endif
}

bool GetFirstProcessWithName(std::string_view name, ProcessId& pid) {
#ifdef PERCEPTION
  if (name.size() > kMaximumProcessNameLength) return false;

  size_t process_name[kMaximumProcessNameLength / 8];
  memset(process_name, 0, kMaximumProcessNameLength);
  memcpy(process_name, &name[0], name.size());

  volatile register size_t syscall asm("rdi") = 22;
#ifndef optimized_BUILD_
  volatile register size_t min_pid asm("r11") = 0;
  volatile register size_t pid_1 asm("r11");
#else
  volatile register size_t min_pid asm("rbp") = 0;
  volatile register size_t pid_1 asm("rbp");
#endif
  volatile register size_t name_1 asm("rax") = process_name[0];
  volatile register size_t name_2 asm("rbx") = process_name[1];
  volatile register size_t name_3 asm("rdx") = process_name[2];
  volatile register size_t name_4 asm("rsi") = process_name[3];
  volatile register size_t name_5 asm("r8") = process_name[4];
  volatile register size_t name_6 asm("r9") = process_name[5];
  volatile register size_t name_7 asm("r10") = process_name[6];
  volatile register size_t name_8 asm("r12") = process_name[7];
  volatile register size_t name_9 asm("r13") = process_name[8];
  volatile register size_t name_10 asm("r14") = process_name[9];
  volatile register size_t name_11 asm("r15") = process_name[10];

  // We only care about the first PID.
  volatile register size_t number_of_processes asm("rdi");
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
  __asm__ __volatile__("syscall\n"
#endif
      : "=r"(number_of_processes), "=r"(pid_1)
      : "r"(syscall), "r"(min_pid), "r"(name_1), "r"(name_2), "r"(name_3),
        "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8),
        "r"(name_9), "r"(name_10), "r"(name_11)
      : "rcx"
#ifdef optimized_BUILD_
        ,
        "r11"
#endif
  );

  if (number_of_processes == 0) return false;

  pid = pid_1;
  return true;
#else
  return false;
#endif
}

void ForEachProcessWithName(
    std::string_view name,
    const std::function<void(ProcessId)>& on_each_process) {
#ifdef PERCEPTION
  if (name.size() > kMaximumProcessNameLength) return;

  size_t process_name[kMaximumProcessNameLength / 8];
  memset(process_name, 0, kMaximumProcessNameLength);
  memcpy(process_name, &name[0], name.size());

  size_t starting_pid = 0;

  // Keep looping over all of the pages until we have a result.
  while (true) {
    // Fetch this page of results.
    volatile register size_t syscall asm("rdi") = 22;
#ifndef optimized_BUILD_
    volatile register size_t starting_pid asm("r11") = 0;
#else
    volatile register size_t starting_pid asm("rbp") = 0;
#endif
    volatile register size_t name_1 asm("rax") = process_name[0];
    volatile register size_t name_2 asm("rbx") = process_name[1];
    volatile register size_t name_3 asm("rdx") = process_name[2];
    volatile register size_t name_4 asm("rsi") = process_name[3];
    volatile register size_t name_5 asm("r8") = process_name[4];
    volatile register size_t name_6 asm("r9") = process_name[5];
    volatile register size_t name_7 asm("r10") = process_name[6];
    volatile register size_t name_8 asm("r12") = process_name[7];
    volatile register size_t name_9 asm("r13") = process_name[8];
    volatile register size_t name_10 asm("r14") = process_name[9];
    volatile register size_t name_11 asm("r15") = process_name[10];

    volatile register size_t number_of_processes_r asm("rdi");

#ifndef optimized_BUILD_
    volatile register size_t pid_1_r asm("r11");
#else
    volatile register size_t pid_1_r asm("rbp");
#endif
    volatile register size_t pid_2_r asm("rax");
    volatile register size_t pid_3_r asm("rbx");
    volatile register size_t pid_4_r asm("rdx");
    volatile register size_t pid_5_r asm("rsi");
    volatile register size_t pid_6_r asm("r8");
    volatile register size_t pid_7_r asm("r9");
    volatile register size_t pid_8_r asm("r10");
    volatile register size_t pid_9_r asm("r12");
    volatile register size_t pid_10_r asm("r13");
    volatile register size_t pid_11_r asm("r14");
    volatile register size_t pid_12_r asm("r15");

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
        : "=r"(number_of_processes_r), "=r"(pid_1_r), "=r"(pid_2_r),
          "=r"(pid_3_r), "=r"(pid_4_r), "=r"(pid_5_r), "=r"(pid_6_r),
          "=r"(pid_7_r), "=r"(pid_8_r), "=r"(pid_9_r), "=r"(pid_10_r),
          "=r"(pid_11_r), "=r"(pid_12_r)
        : "r"(syscall), "r"(starting_pid), "r"(name_1), "r"(name_2),
          "r"(name_3), "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7),
          "r"(name_8), "r"(name_9), "r"(name_10), "r"(name_11)
        : "rcx"
#ifdef optimized_BUILD_
          ,
          "r11"
#endif
    );

    size_t number_of_processes = number_of_processes_r;
    size_t pid_1 = pid_1_r;
    size_t pid_2 = pid_2_r;
    size_t pid_3 = pid_3_r;
    size_t pid_4 = pid_4_r;
    size_t pid_5 = pid_5_r;
    size_t pid_6 = pid_6_r;
    size_t pid_7 = pid_7_r;
    size_t pid_8 = pid_8_r;
    size_t pid_9 = pid_9_r;
    size_t pid_10 = pid_10_r;
    size_t pid_11 = pid_11_r;
    size_t pid_12 = pid_12_r;

    // Call backs.
    if (number_of_processes >= 1) on_each_process(pid_1);
    if (number_of_processes >= 2) on_each_process(pid_2);
    if (number_of_processes >= 3) on_each_process(pid_3);
    if (number_of_processes >= 4) on_each_process(pid_4);
    if (number_of_processes >= 5) on_each_process(pid_5);
    if (number_of_processes >= 6) on_each_process(pid_6);
    if (number_of_processes >= 7) on_each_process(pid_7);
    if (number_of_processes >= 8) on_each_process(pid_8);
    if (number_of_processes >= 9) on_each_process(pid_9);
    if (number_of_processes >= 10) on_each_process(pid_10);
    if (number_of_processes >= 11) on_each_process(pid_11);
    if (number_of_processes >= 12) on_each_process(pid_12);

    // We don't have another page of results.
    if (number_of_processes <= 12) return;

    // Start the next page just afer the last process ID of this page.
    starting_pid = pid_12 + 1;
  }
#endif
}

// Loops through every running process.
void ForEachProcess(const std::function<void(ProcessId)>& on_each_process) {
  ForEachProcessWithName("", on_each_process);
}

// Returns the name of the currently running process.
std::string GetProcessName() { return GetProcessName(GetProcessId()); }

// Returns the name of a process, or an empty string if the process doesn't
// exist.
std::string GetProcessName(ProcessId pid) {
#ifdef PERCEPTION
  // Add an extra byte for the null terminator.
  char process_name[kMaximumProcessNameLength + 1];
  process_name[kMaximumProcessNameLength] = '\0';

  volatile register size_t syscall asm("rdi") = 29;
  volatile register size_t pid_r asm("rax") = pid;

  volatile register size_t was_process_found asm("rdi");
  volatile register size_t name_1 asm("rax");
  volatile register size_t name_2 asm("rbx");
  volatile register size_t name_3 asm("rdx");
  volatile register size_t name_4 asm("rsi");
  volatile register size_t name_5 asm("r8");
  volatile register size_t name_6 asm("r9");
  volatile register size_t name_7 asm("r10");
  volatile register size_t name_8 asm("r12");
  volatile register size_t name_9 asm("r13");
  volatile register size_t name_10 asm("r14");
  volatile register size_t name_11 asm("r15");

  __asm__ __volatile__("syscall\n"
                       : "=r"(was_process_found), "=r"(name_1), "=r"(name_2),
                         "=r"(name_3), "=r"(name_4), "=r"(name_5), "=r"(name_6),
                         "=r"(name_7), "=r"(name_8), "=r"(name_9),
                         "=r"(name_10), "=r"(name_11)
                       : "r"(syscall), "r"(pid_r)
                       : "rcx", "r11");

  if (!was_process_found) return "";

  // Copy the string out of the registers into a char array.
  ((size_t*)process_name)[0] = name_1;
  ((size_t*)process_name)[1] = name_2;
  ((size_t*)process_name)[2] = name_3;
  ((size_t*)process_name)[3] = name_4;
  ((size_t*)process_name)[4] = name_5;
  ((size_t*)process_name)[5] = name_6;
  ((size_t*)process_name)[6] = name_7;
  ((size_t*)process_name)[7] = name_8;
  ((size_t*)process_name)[8] = name_9;
  ((size_t*)process_name)[9] = name_10;
  ((size_t*)process_name)[10] = name_11;

  return std::string(process_name);
#else
  return "";
#endif
}

// Returns true if the process exists.
bool DoesProcessExist(ProcessId pid) {
#ifdef PERCEPTION
  // Add an extra byte for the null terminator.
  char process_name[kMaximumProcessNameLength + 1];
  process_name[kMaximumProcessNameLength] = '\0';

  volatile register size_t syscall asm("rdi") = 29;
  volatile register size_t pid_r asm("rax") = pid;

  volatile register size_t was_process_found asm("rdi");

  __asm__ __volatile__("syscall\n"
                       : "=r"(was_process_found)
                       : "r"(syscall), "r"(pid_r)
                       : "rbx", "rcx", "rdx", "rsi", "r8", "r9", "r10", "r11",
                         "r12", "r13", "r14", "r15");

  return was_process_found;
#endif
}

bool DoesProcessExist(std::string_view name) {
  ProcessId pid;
  return GetFirstProcessWithName(name, pid);
}

// Registers that we want to be notified with a message upon the given process
// terminating. The handler is automatically unregistered upon terminating
// (it's safe to accidentally call StopNotifyingUponProcessTermination if the
// handler has triggered.)
MessageId NotifyUponProcessTermination(
    ProcessId pid, const std::function<void()>& on_termination) {
#ifdef PERCEPTION
  MessageId message_id = GenerateUniqueMessageId();
  RegisterMessageHandler(message_id, [message_id, on_termination](
                                         ProcessId sender, const MessageData&) {
    // We only believe the kernel.
    if (sender != 0) return;
    on_termination();
    UnregisterMessageHandler(message_id);
  });

  volatile register size_t syscall_num asm("rdi") = 30;
  volatile register size_t pid_r asm("rax") = pid;
  volatile register size_t message_id_r asm("rbx") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(pid_r),
                       "r"(message_id_r)
                       : "rcx", "r11");
  return message_id;
#else
  return 0;
#endif
}

// Registers that we don't want to be notified anymore about a process
// terminating.
void StopNotifyingUponProcessTermination(MessageId message_id) {
#ifdef PERCEPTION
  UnregisterMessageHandler(message_id);
  volatile register size_t syscall_num asm("rdi") = 31;
  volatile register size_t message_id_r asm("rax") = message_id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(message_id_r)
                       : "rcx", "r11");
#endif
}

// Creates a child process with a given name. Populates the `pid` with the new
// process's id. The new process doesn't begin executing until
// `StartExecutingChildProcess` is called. Only some processes are allowed to
// create child processes. Returns if the child process was successfully
// created. If the child process hasn't started executing yet, it will terminate
// if this process terminates.
bool CreateChildProcess(std::string_view name, size_t bitfield,
                        ProcessId& pid) {
#ifdef PERCEPTION
  if (name.size() > kMaximumProcessNameLength) return false;

  size_t process_name[kMaximumProcessNameLength / 8];
  memset(process_name, 0, kMaximumProcessNameLength);
  memcpy(process_name, &name[0], name.size());

  volatile register size_t syscall asm("rdi") = 51;
#ifndef optimized_BUILD_
  volatile register size_t bitfield_r asm("r11") = bitfield;
#else
  volatile register size_t bitfield_r asm("rbp") = bitfield;
#endif
  volatile register size_t name_1 asm("rax") = process_name[0];
  volatile register size_t name_2 asm("rbx") = process_name[1];
  volatile register size_t name_3 asm("rdx") = process_name[2];
  volatile register size_t name_4 asm("rsi") = process_name[3];
  volatile register size_t name_5 asm("r8") = process_name[4];
  volatile register size_t name_6 asm("r9") = process_name[5];
  volatile register size_t name_7 asm("r10") = process_name[6];
  volatile register size_t name_8 asm("r12") = process_name[7];
  volatile register size_t name_9 asm("r13") = process_name[8];
  volatile register size_t name_10 asm("r14") = process_name[9];
  volatile register size_t name_11 asm("r15") = process_name[10];

  volatile register size_t pid_r asm("rax");
  __asm__ __volatile__(
#ifndef optimized_BUILD_
      R"(
    push %%rbp
    mov %%r11, %%rbp
    syscall
    pop %%rbp
	)"
#else
      "syscall\n"
#endif
      : "=r"(pid_r)
      : "r"(syscall), "r"(bitfield_r), "r"(name_1), "r"(name_2), "r"(name_3),
        "r"(name_4), "r"(name_5), "r"(name_6), "r"(name_7), "r"(name_8),
        "r"(name_9), "r"(name_10), "r"(name_11)
      : "rcx"
#ifdef optimized_BUILD_
        ,
        "r11"
#endif
  );

  if (pid_r == 0) return false;

  pid = pid_r;
  return true;
#else
  return false;
#endif
}

// Unmaps a memory page from the current process and assigns it to a child
// process that hasn't began executing. The memory is unmapped from this process
// regardless of if this call succeeds. If the page already exists in the child
// process, nothing is set.
void SetChildProcessMemoryPage(ProcessId& child_pid, size_t source_address,
                               size_t destination_address) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 52;
  volatile register size_t pid_r asm("rax") = child_pid;
  volatile register size_t source_r asm("rbx") = source_address;
  volatile register size_t destination_r asm("rdx") = destination_address;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall), "r"(pid_r), "r"(source_r),
                         "r"(destination_r)
                       : "rcx", "r11");
#endif
}

// Creates a thread in the a child process. The child process will begin
// executing and will no longer terminate if the creator terminates.
void StartExecutingChildProcess(ProcessId& child_pid, size_t entry_address,
                                size_t params) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 53;
  volatile register size_t pid_r asm("rax") = child_pid;
  volatile register size_t entry_address_r asm("rbx") = entry_address;
  volatile register size_t params_r asm("rdx") = params;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall), "r"(pid_r), "r"(entry_address_r),
                         "r"(params_r)
                       : "rcx", "r11");
#endif
}

bool IsDuplicateInstanceOfProcess() {
  ProcessId first_pid;
  if (!GetFirstProcessWithName(GetProcessName(), first_pid)) {
    std::cout << "Unable to find myself with GetFirstProcessWithName"
              << std::endl;
    return false;
  }
  return first_pid != GetProcessId();
}

// Destroys a child process that hasn't began executing.
void DestroyChildProcess(ProcessId& child_pid) {
#ifdef PERCEPTION
  volatile register size_t syscall asm("rdi") = 54;
  volatile register size_t pid_r asm("rax") = child_pid;

  __asm__ __volatile__("syscall\n" : : "r"(syscall), "r"(pid_r) : "rcx", "r11");
#endif
}

}  // namespace perception
