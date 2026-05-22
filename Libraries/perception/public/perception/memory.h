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

#pragma once

#include "types.h"

namespace perception {

constexpr size_t kPageSize = 4096;

void* AllocateMemoryPages(size_t number);

void* AllocateMemoryPagesBelowPhysicalAddressBase(
    size_t number, size_t max_base_address, size_t& first_physical_address);

void ReleaseMemoryPages(void* ptr, size_t number);

// Maps physical memory into this process's address space. Only drivers
// may call this.
void* MapPhysicalMemory(size_t physical_address, size_t pages);

size_t GetPhysicalAddressOfVirtualAddress(size_t virtual_address);

bool MaybeResizePages(void** ptr, size_t current_number, size_t new_number);

size_t GetFreeSystemMemory();

size_t GetTotalSystemMemory();

// Exposes the process's health metrics:
// - pid: the ID of the process to query, or 0 for the current process.
// - memory_used: memory allocated to the process in bytes.
// - creation_timestamp: timestamp when the process was created (in microseconds
// since boot).
// - cpu_percentages: array populated with the CPU percentage of each core (size
// must be at least 8).
void GetProcessHealthMetrics(ProcessId pid, size_t& memory_used,
                             size_t& creation_timestamp,
                             uint8* cpu_percentages);

// Controls whether the kernel actively tracks CPU usage metrics for this
// process. This should be set to 'true' by process managers or monitoring
// applications to enable tracking.
void SetThatProcessCaresAboutCpuTracking(bool active);

void SetMemoryAccessRights(void* address, size_t pages, bool can_write,
                           bool can_execute);

}  // namespace perception

// Functions handled by liballoc but redefined here to expose it in this
// library.
extern "C" void* malloc(size_t);
extern "C" void* realloc(void*, size_t);
extern "C" void* calloc(size_t, size_t);
extern "C" void free(void*);
