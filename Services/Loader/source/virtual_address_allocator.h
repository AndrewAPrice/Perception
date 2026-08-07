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

#include <cstddef>
#include <mutex>

#include "perception/aa_tree.h"

struct AddressBlock {
  size_t base_address;
  size_t size;
  ::perception::AATreeNode node;
};

class VirtualAddressAllocator {
 public:
  static VirtualAddressAllocator& Get();

  // Allocates a contiguous virtual address range of size_in_bytes starting at >= 0x4000
  // and records it in the AATree.
  size_t AllocateRange(size_t size_in_bytes);

  // Frees a previously allocated range by base_address.
  void FreeRange(size_t base_address);

  // Finds a free virtual address range of size_in_bytes starting at >= 0x4000
  // without inserting it into the AATree (used for application executables).
  size_t FindFreeRangeWithoutInserting(size_t size_in_bytes);

 private:
  VirtualAddressAllocator() = default;
  ~VirtualAddressAllocator();

  std::mutex mutex_;
  ::perception::AATree<AddressBlock, &AddressBlock::node, &AddressBlock::base_address> allocated_blocks_;
};
