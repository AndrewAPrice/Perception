// Copyright 2026 Google LLC
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

#ifdef TEST

#include "types.h"
#include "scheduling/scheduler.h"
#include "scheduling/thread.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_allocator.h"
#include "memory/virtual_address_space.h"
#include "output/text_terminal.h"
#include "ipc/shared_memory.h"
#include "diagnostics/profiling.h"
#include "interrupts/interrupts.h"
#include "hardware/io.h"
#include <iostream>
#include <iomanip>
#include <unordered_map>

namespace scheduling {

namespace {
Scheduler g_mock_scheduler;
}

Scheduler& Scheduler::Get() {
  return g_mock_scheduler;
}

Scheduler::Scheduler()
    : focused_process_(nullptr), awake_thread_count_(0) {
  for (int i = 0; i < kThreadPriorityCount; i++) queue_credits_[i] = 0;
}

void Scheduler::Initialize() {
  awake_thread_count_ = 0;
}

void Scheduler::ScheduleNextThread() {}

void Scheduler::Schedule(Thread* thread, bool force_preemption) {
  if (thread != nullptr) thread->TransitionToReady();
}

void Scheduler::ScheduleDirectSwitch(Thread* thread) {
  if (thread != nullptr) thread->TransitionToReady();
}

void Scheduler::Unschedule(Thread* thread, ThreadState target_state) {
  if (thread == nullptr) return;
  switch (target_state) {
    case ThreadState::BlockedOnMessage:
      thread->TransitionToBlockedOnMessage();
      break;
    case ThreadState::BlockedOnMemory:
      thread->TransitionToBlockedOnMemory(
          thread->thread_is_waiting_for_shared_memory);
      break;
    case ThreadState::Terminated:
      thread->TransitionToTerminated();
      break;
    case ThreadState::Halted:
    default:
      thread->TransitionToHalted();
      break;
  }
}

void Scheduler::SetPriority(Thread* thread, ThreadPriority priority) {
  if (thread != nullptr) thread->priority = priority;
}

void Scheduler::SetFocusedProcess(processes::Process* process) {
  focused_process_ = process;
}

processes::Process* Scheduler::GetFocusedProcess() {
  return focused_process_;
}

bool Scheduler::HasAwakeThreads() {
  return awake_thread_count_ > 0;
}

bool Scheduler::NeedsTimesliceInterrupt(Thread* thread) {
  return false;
}

void Scheduler::ScheduleIfHalted() {}

// Mocks for Scheduler functions
void InitializeScheduler() {
  Scheduler::Get().Initialize();
}
void ScheduleThread(Thread* thread, bool force_preemption) {
  Scheduler::Get().Schedule(thread, force_preemption);
}
void ScheduleThreadDirectSwitch(Thread* thread) {
  Scheduler::Get().ScheduleDirectSwitch(thread);
}
void UnscheduleThread(Thread* thread, ThreadState target_state) {
  Scheduler::Get().Unschedule(thread, target_state);
}
void SetThreadPriority(Thread* thread, ThreadPriority priority) {
  Scheduler::Get().SetPriority(thread, priority);
}
void ScheduleThreadIfWeAreHalted() {
  Scheduler::Get().ScheduleIfHalted();
}
void ScheduleNextThread() {
  Scheduler::Get().ScheduleNextThread();
}
bool NeedsTimesliceInterrupt(Thread* thread) {
  return Scheduler::Get().NeedsTimesliceInterrupt(thread);
}
void SetFocusedProcess(processes::Process* process) {
  Scheduler::Get().SetFocusedProcess(process);
}
processes::Process* GetFocusedProcess() {
  return Scheduler::Get().GetFocusedProcess();
}
bool HasAwakeThreads() {
  return Scheduler::Get().HasAwakeThreads();
}
}  // namespace scheduling

size_t bssEnd = 0;

namespace memory {
// Mocks for Physical Allocator globals
size_t g_total_system_memory = 0;
size_t g_free_pages = 0;
size_t g_start_of_free_memory_at_boot = 0;

// Mocks for Physical Allocator functions
bool IsPageAlignedAddress(size_t address) {
  return (address & (kPageSize - 1)) == 0;
}

size_t RoundDownToPageAlignedAddress(size_t address) {
  return address & ~(kPageSize - 1);
}

}  // namespace memory

// High-Fidelity Simulated RAM page pool for unit testing
struct PhysicalPageBuffer {
  size_t entries[512]; // zero-initialized array of PML entries
};

std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;
size_t next_mock_physical_page = 0x1000000;

extern "C" size_t mock_cr3 = 0;

namespace memory {

size_t GetPhysicalPage() {
  size_t addr = next_mock_physical_page;
  next_mock_physical_page += 4096;
  simulated_ram[addr] = PhysicalPageBuffer{}; // auto-initializes to 0
  return addr;
}

size_t GetPhysicalPagePreVirtualMemory() {
  return GetPhysicalPage();
}

size_t GetPhysicalPageAtOrBelowAddress(size_t max_base_address) {
  return GetPhysicalPage();
}

void FreePhysicalPage(size_t addr) {
  simulated_ram.erase(addr);
}

void* TemporarilyMapPhysicalPages(size_t addr, size_t index) {
  if (simulated_ram.find(addr) == simulated_ram.end()) {
    simulated_ram[addr] = PhysicalPageBuffer{};
  }
  return &simulated_ram[addr].entries;
}

void* TemporarilyMapPhysicalMemoryPreVirtualMemory(size_t addr, size_t index) {
  return TemporarilyMapPhysicalPages(addr, index);
}
}  // namespace memory


// Mocks for Profiling functions
namespace diagnostics {
void NotifyProfilerThatProcessExited(processes::Process* proc) {}
}

// Mocks for Interrupt functions
namespace interrupts {
void UnregisterAllMessagesToFireOnInterruptForProcess(processes::Process* proc) {}
}

// Mocks for IO functions
namespace hardware {
void WriteIOByte(unsigned short port, unsigned char data) {}
void WriteModelSpecificRegister(uint64 msr, uint64 value) {}
uint64 ReadModelSpecificRegister(uint64 msr) { return 0; }
uint64 ReadTimestampCounter() { return 0; }
void GetCpuId(uint32 leaf, uint32& eax, uint32& ebx, uint32& ecx, uint32& edx) {
  eax = ebx = ecx = edx = 0;
}
void GetCpuId(uint32 leaf, uint32 subleaf, uint32& eax, uint32& ebx, uint32& ecx,
              uint32& edx) {
  eax = ebx = ecx = edx = 0;
}
}  // namespace hardware

// Mocks for debug Printer
namespace output {
Printer print;

Printer::Printer() : number_format_(NumberFormat::Decimal) {}

Printer& Printer::operator<<(char c) {
  std::cout << c;
  return *this;
}

Printer& Printer::operator<<(const char* str) {
  std::cout << str;
  return *this;
}

Printer& Printer::operator<<(int c) {
  std::cout << c;
  return *this;
}

Printer& Printer::operator<<(size_t num) {
  if (number_format_ == NumberFormat::Hexidecimal) {
    std::cout << "0x" << std::hex << num << std::dec;
  } else {
    std::cout << num;
  }
  return *this;
}

Printer& Printer::operator<<(NumberFormat format) {
  number_format_ = format;
  return *this;
}
}  // namespace output

#endif // TEST
