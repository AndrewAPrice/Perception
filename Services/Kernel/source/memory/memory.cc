// Copyright 2024 Google LLC
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
#include "memory/memory.h"

#include "memory/physical_allocator.h"
#include "processes/process.h"
#include "memory/virtual_allocator.h"

#ifdef TEST
#include <stdio.h>
#else
extern "C" void* memcpy(void *dest, const void *src, size_t count) {
  void *ret = dest;
  size_t qwords = count >> 3;
  size_t bytes = count & 7;
  asm volatile("rep movsq; mov %3, %%rcx; rep movsb"
               : "+D"(dest), "+S"(src), "+c"(qwords)
               : "r"(bytes)
               : "memory");
  return ret;
}

extern "C" void* memset(void *dest, int val, size_t count) {
  void *ret = dest;
  uint64 val64 = static_cast<uint8>(val);
  val64 |= (val64 << 8);
  val64 |= (val64 << 16);
  val64 |= (val64 << 32);
  size_t qwords = count >> 3;
  size_t bytes = count & 7;
  asm volatile("rep stosq; mov %3, %%rcx; rep stosb"
               : "+D"(dest), "+c"(qwords)
               : "a"(val64), "r"(bytes)
               : "memory");
  return ret;
}
#endif

namespace memory {

using processes::Process;

bool IsKernelAddress(size_t address) {
  return address >= kVirtualMemoryOffset;
}

namespace {

// Iterates over process memory pages covering [to_start, to_end), mapping each
// page and passing the mapped virtual buffer and span length to `op`.
template <typename PageOp>
bool ForEachProcessMemorySpan(size_t to_start, size_t to_end, Process *process,
                              PageOp&& op) {
  // An empty range is trivially handled. An inverted one would underflow the
  // per-page length below into a span far larger than the temporary mapping.
  if (to_end <= to_start) return to_end == to_start;

  VirtualAddressSpace &address_space = process->virtual_address_space;

  size_t to_first_page = RoundDownToPageAlignedAddress(to_start);
  size_t to_last_page = RoundUpToPageAlignedAddress(to_end);
  // Rounding up wraps to zero for an end address in the last page of the
  // address space, which would silently copy nothing.
  if (to_last_page < to_end) return false;

  for (size_t to_page = to_first_page; to_page < to_last_page;
       to_page += kPageSize) {
    size_t physical_page_address =
        address_space.GetOrCreateVirtualPage(to_page);
    if (physical_page_address == kOutOfMemory)
      return false;

    size_t temp_addr =
        (size_t)TemporarilyMapPhysicalPages(physical_page_address, 5);

    size_t offset_in_page_to_start =
        to_start > to_page ? to_start - to_page : 0;
    size_t offset_in_page_to_finish =
        to_page + kPageSize > to_end ? to_end - to_page : kPageSize;
    size_t length = offset_in_page_to_finish - offset_in_page_to_start;

    op((char *)(temp_addr + offset_in_page_to_start), length);
  }

  return true;
}

}  // namespace

bool CopyKernelMemoryIntoProcess(size_t from_start, size_t to_start,
                                 size_t to_end, Process *process) {
  return ForEachProcessMemorySpan(
      to_start, to_end, process, [&](char *dest, size_t length) {
        memcpy(dest, (const char *)from_start, length);
        from_start += length;
      });
}

bool ZeroProcessMemory(size_t to_start, size_t to_end, Process *process) {
  return ForEachProcessMemorySpan(
      to_start, to_end, process,
      [](char *dest, size_t length) { memset(dest, 0, length); });
}

size_t PagesThatContainBytes(size_t bytes) {
  return (bytes + kPageSize - 1) / kPageSize;
}

}  // namespace memory

