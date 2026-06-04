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

#include "perception/threads.h"

#if !defined(PERCEPTION) || defined(TEST)
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#endif

namespace perception {
namespace {

thread_local __attribute__((tls_model("initial-exec"))) bool is_primary_thread =
    false;
ThreadId primary_thread_id = 0;

__attribute__((constructor)) void InitializePrimaryThread() {
  is_primary_thread = true;
  primary_thread_id = GetThreadId();
}

}  // namespace

ThreadId CreateThread(void (*entry_point)(void*), void* param) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 1;
  volatile register size_t param1 asm("rax") = (size_t)entry_point;
  volatile register size_t param2 asm("rbx") = (size_t)param;
  volatile register size_t param3 asm("rdx") = 0;
  volatile register size_t param4 asm("rsi") = 0;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num), "r"(param1), "r"(param2),
                         "r"(param3), "r"(param4)
                       : "rcx", "r11");
  return return_val;
#else
  std::cout << "You shouldn't manually call perception::CreateThread. Please "
               "use std::thread."
            << std::endl;
  return 0;
#endif
}

ThreadId GetThreadId() {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 2;
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num)
                       : "rcx", "r11");
  return return_val;
#else
  std::stringstream ss;
  ss << std::this_thread::get_id();
  return (ThreadId)std::stoi(ss.str());
#endif
}

void TerminateThread() {
#if defined(PERCEPTION) && !defined(TEST)
  register size_t syscall_num asm("rdi") = 4;
  __asm__("syscall\n" ::"r"(syscall_num) : "rcx", "r11");
#else
  std::cout << "You shouldn't manually call perception::TerminateThread. "
               "Please let the thread function return."
            << std::endl;
#endif
}

void TerminateThread(ThreadId tid) {
#if defined(PERCEPTION) && !defined(TEST)
  register size_t syscall_num asm("rdi") = 5;
  register size_t param asm("rax") = tid;

  __asm__("syscall\n" ::"r"(syscall_num), "r"(param) : "rcx", "r11");
#else
  std::cout << "You shouldn't manually call perception::TerminateThread. "
               "Please let the thread function return."
            << std::endl;
#endif
}

void SetThreadSegment(size_t segment_address) {
#if defined(PERCEPTION) && !defined(TEST)
  __asm__ __volatile__("syscall\n"
                       :
                       : "D"((size_t)27), "a"(segment_address)
                       : "rcx", "r11", "memory");
#endif
}

void SetThreadSegments(std::optional<size_t> fs_address,
                       std::optional<size_t> gs_address) {
#if defined(PERCEPTION) && !defined(TEST)
  size_t mask = 0;
  if (fs_address) mask |= 1;
  if (gs_address) mask |= 2;

  __asm__ __volatile__("syscall\n"
                       :
                       : "D"((size_t)63), "a"(fs_address.value_or(0)),
                         "b"(gs_address.value_or(0)), "d"(mask)
                       : "rcx", "r11", "memory");
#endif
}

void SetAddressToClearOnThreadTermination(size_t address_to_clear) {
#if defined(PERCEPTION) && !defined(TEST)
  register size_t syscall_num asm("rdi") = 28;
  register size_t param asm("rax") = address_to_clear;

  __asm__("syscall\n" ::"r"(syscall_num), "r"(param) : "rcx", "r11");
#endif
}

bool SetThreadPriority(ThreadPriority priority) {
  return SetThreadPriority(0, priority);
}

bool SetThreadPriority(ThreadId tid, ThreadPriority priority) {
#if defined(PERCEPTION) && !defined(TEST)
  volatile register size_t syscall_num asm("rdi") = 65;
  volatile register size_t param1 asm("rax") = tid;
  volatile register size_t param2 asm("rbx") = static_cast<size_t>(priority);
  volatile register size_t return_val asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(return_val)
                       : "r"(syscall_num), "r"(param1), "r"(param2)
                       : "rcx", "r11");
  return return_val == 0;
#else
  return true;
#endif
}

bool IsPrimaryThread() { return is_primary_thread; }

ThreadId GetPrimaryThreadId() { return primary_thread_id; }

}  // namespace perception

#if defined(PERCEPTION) && !defined(TEST)

extern "C" {

struct ThreadDestructor {
  void (*dtor)(void*);
  void* obj;
};

// Thread-local list of registered destructors.
thread_local __attribute__((tls_model(
    "initial-exec"))) std::vector<ThreadDestructor>* thread_destructors =
    nullptr;

static pthread_key_t destructor_key;
static pthread_once_t destructor_key_once = PTHREAD_ONCE_INIT;

static void run_thread_destructors(void* arg) {
  auto* destructors = static_cast<std::vector<ThreadDestructor>*>(arg);
  if (destructors == nullptr) return;

  // Run destructors in reverse order (standard C++ behavior)
  for (auto it = destructors->rbegin(); it != destructors->rend(); ++it) {
    if (it->dtor) {
      it->dtor(it->obj);
    }
  }
  delete destructors;
  thread_destructors = nullptr;
}

static void create_destructor_key() {
  pthread_key_create(&destructor_key, run_thread_destructors);
}

int __cxa_thread_atexit(void (*dtor)(void*), void* obj, void* dso_handle) {
  pthread_once(&destructor_key_once, create_destructor_key);

  if (thread_destructors == nullptr) {
    thread_destructors = new std::vector<ThreadDestructor>();
    pthread_setspecific(destructor_key, thread_destructors);
  }

  thread_destructors->push_back({dtor, obj});
  return 0;
}

}  // extern "C"
#endif
