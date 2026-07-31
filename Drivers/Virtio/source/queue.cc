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

constexpr size_t kMax32BitAddress = 0xFFFFFFFF;
constexpr size_t kDescTableEntrySize = 16;
constexpr size_t kAvailRingHeaderSize = 6;
constexpr size_t kAvailRingElementSize = 2;
constexpr size_t kUsedRingHeaderSize = 6;
constexpr size_t kUsedRingElementSize = 8;
constexpr size_t kPageMask = 4095;
constexpr size_t kCommonCfgQueueSelectOffset = 22;
constexpr size_t kCommonCfgQueueSizeOffset = 24;
constexpr size_t kCommonCfgQueueMsixVectorOffset = 26;
constexpr size_t kCommonCfgQueueEnableOffset = 28;
constexpr size_t kCommonCfgQueueNotifyOffOffset = 30;
constexpr size_t kCommonCfgQueueDescOffset = 32;
constexpr size_t kCommonCfgQueueAvailOffset = 40;
constexpr size_t kCommonCfgQueueUsedOffset = 48;
constexpr size_t kCommonCfgSize = 64;

}  // namespace

void* AllocateContiguousMemoryPages(size_t pages, size_t& physical_address) {
  if (pages == 0) return nullptr;
  int attempts = 32;
  while (attempts-- > 0) {
    void* virt_addr = AllocateMemoryPagesBelowPhysicalAddressBase(
        pages, kMax32BitAddress, physical_address);
    if (!virt_addr) return nullptr;
    if (pages == 1) return virt_addr;

    size_t phys0 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr);
    bool contiguous = true;
    for (size_t i = 1; i < pages; i++) {
      if (GetPhysicalAddressOfVirtualAddress(
              (size_t)virt_addr + i * kPageSize) != phys0 + i * kPageSize) {
        contiguous = false;
        break;
      }
    }

    if (contiguous) {
      physical_address = phys0;
      return virt_addr;
    }

    ReleaseMemoryPages(virt_addr, pages);
  }
  return nullptr;
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

  size_t physical_address = 0;
  void* virt_addr = AllocateContiguousMemoryPages(pages, physical_address);
  if (!virt_addr) return;

  memset(virt_addr, 0, pages * kPageSize);

  size = qsize;
  mem = virt_addr;
  phys = physical_address;
  last_seen_used = 0;
  next_desc = 0;

  desc = (volatile VirtQueueDesc*)virt_addr;
  avail = (volatile VirtQueueAvail*)((size_t)virt_addr + desc_table_size);
  used = (volatile VirtQueueUsed*)((size_t)virt_addr + used_ring_offset);

  for (int i = 0; i < qsize; i++) {
    buffers_virt[i] = AllocateMemoryPagesBelowPhysicalAddressBase(
        1, kMax32BitAddress, buffers_phys[i]);
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

  void* desc_virt = AllocateMemoryPages(1);
  void* avail_virt = AllocateMemoryPages(1);
  void* used_virt = AllocateMemoryPages(1);
  if (!desc_virt || !avail_virt || !used_virt) return;

  memset(desc_virt, 0, kPageSize);
  memset(avail_virt, 0, kPageSize);
  memset(used_virt, 0, kPageSize);

  size = qsize;
  mem = desc_virt;
  phys = GetPhysicalAddressOfVirtualAddress((size_t)desc_virt);
  last_seen_used = 0;
  next_desc = 0;

  desc = (volatile VirtQueueDesc*)desc_virt;
  avail = (volatile VirtQueueAvail*)avail_virt;
  used = (volatile VirtQueueUsed*)used_virt;

  for (int i = 0; i < qsize; i++) {
    buffers_virt[i] = AllocateMemoryPages(1);
    buffers_phys[i] =
        GetPhysicalAddressOfVirtualAddress((size_t)buffers_virt[i]);
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
