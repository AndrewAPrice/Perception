// Copyright 2023 Google LLC
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

#include "profiling.h"

#ifdef PROFILING_ENABLED

#include "io.h"
#include "syscall.h"
#include "text_terminal.h"

struct SyscallProfilingInformation {
  size_t total_time;
  size_t count;
  size_t shortest_time;
  size_t average_time;
  size_t longest_time;
};

struct SyscallProfilingInformation
    syscall_profiling_information[NUMBER_OF_SYSCALLS];

size_t CurrentTimeForProfiling() {
  unsigned hi, lo;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi) << 32);
}

void ProfileSyscall(int syscall, size_t start_time) {
  size_t end_time = CurrentTimeForProfiling();
  size_t time = end_time - start_time;
  struct SyscallProfilingInformation* syscall_info =
      &syscall_profiling_information[syscall];

  syscall_info->count++;
  syscall_info->total_time += time;
  if (time < syscall_info->shortest_time) syscall_info->shortest_time = time;
  syscall_info->average_time = syscall_info->total_time / syscall_info->count;
  if (time > syscall_info->longest_time) syscall_info->longest_time = time;
}

void PrintProfilingInformation() {
  PrintString("\nProfiling information for syscalls:\n");
  PrintString(
      "syscall,name,count,total_time,shortest_time,average_time,longest_"
      "time\n");
  for (int i = 0; i < NUMBER_OF_SYSCALLS; i++) {
    struct SyscallProfilingInformation* syscall_info =
        &syscall_profiling_information[i];
    PrintNumberWithoutCommas(i);
    PrintChar(',');
    PrintString(GetSystemCallName(i));
    PrintChar(',');
    PrintNumberWithoutCommas(syscall_info->count);
    PrintChar(',');
    PrintNumberWithoutCommas(syscall_info->total_time);
    PrintChar(',');
    PrintNumberWithoutCommas(
        syscall_info->count == 0 ? 0 : syscall_info->shortest_time);
    PrintChar(',');
    PrintNumberWithoutCommas(syscall_info->average_time);
    PrintChar(',');
    PrintNumberWithoutCommas(syscall_info->longest_time);
    PrintChar('\n');
  }
}

#endif

void InitializeProfiling() {
#ifdef PROFILING_ENABLED
  size_t size_of_new_table =
      sizeof(struct SyscallProfilingInformation) * NUMBER_OF_SYSCALLS;
  memset(syscall_profiling_information, 0, size_of_new_table);

  for (int s = 0; s < NUMBER_OF_SYSCALLS; s++)
    syscall_profiling_information[s].shortest_time = 0xFFFFFFFFFFFFFFFF;
#endif
}