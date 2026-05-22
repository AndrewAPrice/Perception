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
#include "scheduler.h"
#include "physical_allocator.h"
#include "virtual_allocator.h"
#include "virtual_address_space.h"
#include "text_terminal.h"
#include "shared_memory.h"
#include "profiling.h"
#include "interrupts.h"
#include "io.h"
#include <iostream>
#include <iomanip>
#include <unordered_map>

// Mocks for Scheduler globals
Thread *running_thread = nullptr;
Registers *currently_executing_thread_regs = nullptr;

// Mocks for Scheduler functions
void ScheduleThread(Thread *thread) {}
void UnscheduleThread(Thread *thread) {}
void ScheduleThreadIfWeAreHalted() {}
void ScheduleNextThread() {}
bool HasAtLeast2AwakeThreads() { return false; }

// Mocks for Physical Allocator globals
size_t total_system_memory = 0;
size_t free_pages = 0;
size_t start_of_free_memory_at_boot = 0;
size_t bssEnd = 0;

// Mocks for Physical Allocator functions
bool IsPageAlignedAddress(size_t address) {
  return (address & (PAGE_SIZE - 1)) == 0;
}

size_t RoundDownToPageAlignedAddress(size_t address) {
  return address & ~(PAGE_SIZE - 1);
}

// High-Fidelity Simulated RAM page pool for unit testing
struct PhysicalPageBuffer {
  size_t entries[512]; // zero-initialized array of PML entries
};

std::unordered_map<size_t, PhysicalPageBuffer> simulated_ram;
size_t next_mock_physical_page = 0x1000000;
size_t mock_cr3 = 0;

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

// Mocks for Profiling functions
void NotifyProfilerThatProcessExited(Process* proc) {}

// Mocks for Interrupt functions
void UnregisterAllMessagesToForOnInterruptForProcess(Process* proc) {}

// Mocks for IO functions
void WriteIOByte(unsigned short port, unsigned char data) {}
void WriteModelSpecificRegister(uint64 msr, uint64 value) {}
uint64 ReadModelSpecificRegister(uint64 msr) { return 0; }
uint64 ReadTimestampCounter() { return 0; }
void GetCpuId(uint32 leaf, uint32* eax, uint32* ebx, uint32* ecx, uint32* edx) {
  *eax = *ebx = *ecx = *edx = 0;
}

// Mocks for debug Printer
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

#endif // TEST
