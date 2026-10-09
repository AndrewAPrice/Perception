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

#include "queue.h"

#include <string.h>

#include "perception/cache.h"
#include "perception/memory.h"
#include "perception/port_io.h"
#include "types.h"
#include "virtio.h"

using ::perception::AllocateMemoryPages;
using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::FlushRange;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::Read16BitsFromPort;
using ::perception::ReleaseMemoryPages;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;

namespace {

// Highest physical address below 4GB.
constexpr size_t kMax32BitAddress = 0xFFFFFFFF;
// Descriptor table entry byte size.
constexpr size_t kDescTableEntrySize = 16;
// Available ring header byte size.
constexpr size_t kAvailRingHeaderSize = 6;
// Available ring element byte size.
constexpr size_t kAvailRingElementSize = 2;
// Used ring header byte size.
constexpr size_t kUsedRingHeaderSize = 6;
// Used ring element byte size.
constexpr size_t kUsedRingElementSize = 8;
// Page offset bitmask.
constexpr size_t kPageMask = 4095;
// Queue select register offset in modern common config.
constexpr size_t kCommonCfgQueueSelectOffset = 22;
// Queue size register offset in modern common config.
constexpr size_t kCommonCfgQueueSizeOffset = 24;
// MSI-X vector register offset in modern common config.
constexpr size_t kCommonCfgQueueMsixVectorOffset = 26;
// Queue enable register offset in modern common config.
constexpr size_t kCommonCfgQueueEnableOffset = 28;
// Queue notify offset register offset in modern common config.
constexpr size_t kCommonCfgQueueNotifyOffOffset = 30;
// Queue descriptor table address register offset in modern common config.
constexpr size_t kCommonCfgQueueDescOffset = 32;
// Queue available ring address register offset in modern common config.
constexpr size_t kCommonCfgQueueAvailOffset = 40;
// Queue used ring address register offset in modern common config.
constexpr size_t kCommonCfgQueueUsedOffset = 48;
// Modern common config structure byte size.
constexpr size_t kCommonCfgSize = 64;
// Maximum single pages to search when freelist fragmentation prevents contiguous allocation.
constexpr size_t kMaxSearchPages = 128;

struct PageEntry {
  void* virt;
  size_t phys;
};

bool IsAscendingContiguous(void* virt_addr, size_t pages, size_t base_phys) {
  for (size_t i = 1; i < pages; i++) {
    if (GetPhysicalAddressOfVirtualAddress((size_t)virt_addr + i * kPageSize) !=
        base_phys + i * kPageSize)
      return false;
  }
  return true;
}

bool IsDescendingContiguous(void* virt_addr, size_t pages, size_t base_phys) {
  for (size_t i = 1; i < pages; i++) {
    if (GetPhysicalAddressOfVirtualAddress((size_t)virt_addr + i * kPageSize) !=
        base_phys - i * kPageSize)
      return false;
  }
  return true;
}

}  // namespace

void* AllocateContiguousMemoryPages(size_t pages, size_t& physical_address) {
  if (pages == 0) return nullptr;
  if (pages == 1)
    return AllocateMemoryPagesBelowPhysicalAddressBase(1, kMax32BitAddress,
                                                       physical_address);

  void* virt_addr = AllocateMemoryPagesBelowPhysicalAddressBase(
      pages, kMax32BitAddress, physical_address);
  if (!virt_addr) return nullptr;

  size_t phys0 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr);
  if (IsAscendingContiguous(virt_addr, pages, phys0)) {
    physical_address = phys0;
    return virt_addr;
  }

  if (IsDescendingContiguous(virt_addr, pages, phys0)) {
    ReleaseMemoryPages(virt_addr, pages);
    virt_addr = AllocateMemoryPagesBelowPhysicalAddressBase(
        pages, kMax32BitAddress, physical_address);
    if (virt_addr) {
      phys0 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr);
      if (IsAscendingContiguous(virt_addr, pages, phys0)) {
        physical_address = phys0;
        return virt_addr;
      }
      ReleaseMemoryPages(virt_addr, pages);
    }
  } else {
    ReleaseMemoryPages(virt_addr, pages);
  }

  if (pages > kMaxSearchPages) return nullptr;

  PageEntry allocated[kMaxSearchPages];
  size_t allocated_count = 0;
  void* result = nullptr;

  while (allocated_count < kMaxSearchPages) {
    size_t single_phys = 0;
    void* single_virt = AllocateMemoryPagesBelowPhysicalAddressBase(
        1, kMax32BitAddress, single_phys);
    if (!single_virt) break;

    size_t insert_index = allocated_count;
    while (insert_index > 0 &&
           allocated[insert_index - 1].phys > single_phys) {
      allocated[insert_index] = allocated[insert_index - 1];
      insert_index--;
    }
    allocated[insert_index].virt = single_virt;
    allocated[insert_index].phys = single_phys;
    allocated_count++;

    if (allocated_count >= pages) {
      size_t contiguous_run = 1;
      size_t match_start_index = 0;
      for (size_t index = 1; index < allocated_count; index++) {
        if (allocated[index].phys ==
            allocated[index - 1].phys + kPageSize) {
          contiguous_run++;
          if (contiguous_run == pages) {
            match_start_index = index - pages + 1;
            break;
          }
        } else {
          contiguous_run = 1;
        }
      }

      if (contiguous_run == pages) {
        for (size_t offset = pages; offset > 0; offset--) {
          ReleaseMemoryPages(allocated[match_start_index + offset - 1].virt, 1);
        }

        result = AllocateMemoryPagesBelowPhysicalAddressBase(
            pages, kMax32BitAddress, physical_address);

        for (size_t offset = 0; offset < allocated_count; offset++) {
          if (offset < match_start_index ||
              offset >= match_start_index + pages) {
            ReleaseMemoryPages(allocated[offset].virt, 1);
          }
        }
        allocated_count = 0;

        if (result) {
          size_t result_phys =
              GetPhysicalAddressOfVirtualAddress((size_t)result);
          if (IsAscendingContiguous(result, pages, result_phys)) {
            physical_address = result_phys;
            return result;
          }
          ReleaseMemoryPages(result, pages);
          result = nullptr;
        }
        break;
      }
    }
  }

  for (size_t index = 0; index < allocated_count; index++) {
    ReleaseMemoryPages(allocated[index].virt, 1);
  }

  return result;
}

void QueueDetails::Setup(uint16 queue_idx, uint16 io_base) {
  Write16BitsToPort(io_base + kVirtioPciQueueSel, queue_idx);
  uint16 qsize = Read16BitsFromPort(io_base + kVirtioPciQueueNum);
  if (qsize == 0) return;
  if (qsize > kMaxQueueSize) qsize = kMaxQueueSize;

  size_t desc_table_size = qsize * kDescTableEntrySize;
  size_t avail_ring_size = kAvailRingHeaderSize + qsize * kAvailRingElementSize;
  size_t used_ring_offset =
      (desc_table_size + avail_ring_size + kPageMask) & ~kPageMask;
  size_t used_ring_size = kUsedRingHeaderSize + qsize * kUsedRingElementSize;
  size_t total_size = used_ring_offset + used_ring_size;
  size_t pages = (total_size + kPageMask) / kPageSize;

  void* virt_addr = mem;
  size_t physical_address = phys;
  bool already_allocated =
      (virt_addr != nullptr && mem_size == pages * kPageSize);
  if (!already_allocated) {
    virt_addr = AllocateContiguousMemoryPages(pages, physical_address);
    if (!virt_addr) return;
  }

  memset(virt_addr, 0, pages * kPageSize);

  size = qsize;
  queue_index = queue_idx;
  mem = virt_addr;
  mem_size = pages * kPageSize;
  phys = physical_address;
  last_seen_used = 0;
  next_desc = 0;

  desc = (volatile VirtQueueDesc*)virt_addr;
  avail = (volatile VirtQueueAvail*)((size_t)virt_addr + desc_table_size);
  used = (volatile VirtQueueUsed*)((size_t)virt_addr + used_ring_offset);

  for (int i = 0; i < qsize; i++) {
    if (!buffers_virt[i]) {
      buffers_virt[i] = AllocateMemoryPagesBelowPhysicalAddressBase(
          1, kMax32BitAddress, buffers_phys[i]);
    }
  }

  avail->flags = 0;
  avail->idx = 0;

  FlushRange(virt_addr, pages * kPageSize);
  Write32BitsToPort(io_base + kVirtioPciQueuePfn, physical_address / kPageSize);
}

void QueueDetails::SetupModern(uint16 queue_idx, volatile uint8* common_cfg) {
  *(volatile uint16*)(&common_cfg[kCommonCfgQueueSelectOffset]) = queue_idx;
  uint16 qsize = *(volatile uint16*)(&common_cfg[kCommonCfgQueueSizeOffset]);
  if (qsize == 0) return;
  if (qsize > kMaxQueueSize) qsize = kMaxQueueSize;

  *(volatile uint16*)(&common_cfg[kCommonCfgQueueSizeOffset]) = qsize;

  notify_off = *(volatile uint16*)(&common_cfg[kCommonCfgQueueNotifyOffOffset]);

  void* desc_virt = (void*)desc;
  void* avail_virt = (void*)avail;
  void* used_virt = (void*)used;
  if (!desc_virt) desc_virt = AllocateMemoryPages(1);
  if (!avail_virt) avail_virt = AllocateMemoryPages(1);
  if (!used_virt) used_virt = AllocateMemoryPages(1);
  if (!desc_virt || !avail_virt || !used_virt) return;

  memset(desc_virt, 0, kPageSize);
  memset(avail_virt, 0, kPageSize);
  memset(used_virt, 0, kPageSize);

  size = qsize;
  queue_index = queue_idx;
  mem = desc_virt;
  mem_size = kPageSize;
  phys = GetPhysicalAddressOfVirtualAddress((size_t)desc_virt);
  last_seen_used = 0;
  next_desc = 0;

  desc = (volatile VirtQueueDesc*)desc_virt;
  avail = (volatile VirtQueueAvail*)avail_virt;
  used = (volatile VirtQueueUsed*)used_virt;

  for (int i = 0; i < qsize; i++) {
    if (!buffers_virt[i]) {
      buffers_virt[i] = AllocateMemoryPages(1);
      buffers_phys[i] =
          GetPhysicalAddressOfVirtualAddress((size_t)buffers_virt[i]);
    }
  }

  avail->flags = 0;
  avail->idx = 0;

  FlushRange(desc_virt, kPageSize);
  FlushRange(avail_virt, kPageSize);
  FlushRange(used_virt, kPageSize);

  uint64 desc_p = GetPhysicalAddressOfVirtualAddress((size_t)desc_virt);
  *(volatile uint64*)(&common_cfg[kCommonCfgQueueDescOffset]) = desc_p;

  uint64 avail_p = GetPhysicalAddressOfVirtualAddress((size_t)avail_virt);
  *(volatile uint64*)(&common_cfg[kCommonCfgQueueAvailOffset]) = avail_p;

  uint64 used_p = GetPhysicalAddressOfVirtualAddress((size_t)used_virt);
  *(volatile uint64*)(&common_cfg[kCommonCfgQueueUsedOffset]) = used_p;

  // Set MSI-X vector to NO_VECTOR (0xFFFF) BEFORE enabling queue (VirtIO 1.1 Spec 4.1.4.3)
  *(volatile uint16*)(&common_cfg[kCommonCfgQueueMsixVectorOffset]) = 0xFFFF;

  *(volatile uint16*)(&common_cfg[kCommonCfgQueueEnableOffset]) =
      1;  // Enable queue
}
