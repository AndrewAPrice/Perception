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
#pragma once

#include "types.h"

// Virtio Descriptor structure
struct VirtQueueDesc {
  uint64 addr;
  uint32 len;
  uint16 flags;
  uint16 next;
} __attribute__((packed));

constexpr uint16 kMaxQueueSize = 256;

// Virtio Available Ring structure
struct VirtQueueAvail {
  uint16 flags;
  uint16 idx;
  uint16 ring[kMaxQueueSize];
} __attribute__((packed));

// Virtio Used Ring Element
struct VirtQueueUsedElem {
  uint32 id;
  uint32 len;
} __attribute__((packed));

// Virtio Used Ring structure
struct VirtQueueUsed {
  uint16 flags;
  uint16 idx;
  VirtQueueUsedElem ring[kMaxQueueSize];
} __attribute__((packed));

struct QueueDetails {
  uint16 size = 0;
  void* mem = nullptr;
  size_t phys = 0;
  volatile VirtQueueDesc* desc = nullptr;
  volatile VirtQueueAvail* avail = nullptr;
  volatile VirtQueueUsed* used = nullptr;
  uint16 last_seen_used = 0;
  uint16 next_desc = 0;
  uint16 notify_off = 0;
  void* buffers_virt[kMaxQueueSize] = {};
  size_t buffers_phys[kMaxQueueSize] = {};

  void Setup(uint16 queue_idx, uint16 io_base);
  void SetupModern(uint16 queue_idx, volatile uint8* common_cfg);
};

// Allocate contiguous physical pages.
void* AllocateContiguousMemoryPages(size_t pages, size_t& physical_address);
