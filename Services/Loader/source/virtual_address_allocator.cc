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

#include "virtual_address_allocator.h"

#include <algorithm>

namespace {
constexpr size_t kMinVirtualAddress = 0x2000000;
constexpr size_t kPageSize = 4096;

size_t RoundUpToPage(size_t size) {
  return (size + kPageSize - 1) & ~(kPageSize - 1);
}
}  // namespace

VirtualAddressAllocator& VirtualAddressAllocator::Get() {
  static VirtualAddressAllocator instance;
  return instance;
}

VirtualAddressAllocator::~VirtualAddressAllocator() {
  std::lock_guard<std::mutex> lock(mutex_);
  while (!allocated_blocks_.IsEmpty()) {
    AddressBlock* block = allocated_blocks_.FirstItem();
    allocated_blocks_.Remove(block);
    delete block;
  }
}

size_t VirtualAddressAllocator::FindFreeRangeWithoutInserting(size_t size_in_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t needed_size = RoundUpToPage(size_in_bytes);
  size_t candidate_addr = kMinVirtualAddress;

  for (AddressBlock* block = allocated_blocks_.FirstItem(); block != nullptr;
       block = allocated_blocks_.NextItem(block)) {
    if (candidate_addr + needed_size <= block->base_address) {
      return candidate_addr;
    }
    candidate_addr = std::max(candidate_addr, RoundUpToPage(block->base_address + block->size));
  }

  return candidate_addr;
}

size_t VirtualAddressAllocator::AllocateRange(size_t size_in_bytes) {
  std::lock_guard<std::mutex> lock(mutex_);
  size_t needed_size = RoundUpToPage(size_in_bytes);
  size_t candidate_addr = kMinVirtualAddress;

  for (AddressBlock* block = allocated_blocks_.FirstItem(); block != nullptr;
       block = allocated_blocks_.NextItem(block)) {
    if (candidate_addr + needed_size <= block->base_address) {
      break;
    }
    candidate_addr = std::max(candidate_addr, RoundUpToPage(block->base_address + block->size));
  }

  AddressBlock* new_block = new AddressBlock();
  new_block->base_address = candidate_addr;
  new_block->size = needed_size;
  allocated_blocks_.Insert(new_block);

  return candidate_addr;
}

void VirtualAddressAllocator::FreeRange(size_t base_address) {
  std::lock_guard<std::mutex> lock(mutex_);
  AddressBlock* block = allocated_blocks_.SearchForItemEqualToValue(base_address);
  if (block != nullptr) {
    allocated_blocks_.Remove(block);
    delete block;
  }
}
