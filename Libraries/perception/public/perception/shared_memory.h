// Copyright 2021 Google LLC
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

#include <functional>
#include <memory>

#include "types.h"

namespace perception {

// Represents a memory block that can be shared between multiple processess.
// Shared memory is reference counted. The reference counter is increased
// the first time you try to dereference it, or when you call Join(). The
// counter is decreased when SharedMemory leaves scope. The memory is released
// when the reference counter reaches zero.
class SharedMemory {
 public:
  // Flags:
  // The shared memory buffer is lazily allocated.
  static constexpr size_t kLazilyAllocated = 1;
  // Joiners can write to the shared memory buffer.
  static constexpr size_t kJoinersCanWrite = 1 << 1;

  SharedMemory();

  // Allow moving the object with std::move.
  SharedMemory(SharedMemory&& other);
  SharedMemory& operator=(SharedMemory&& other);

  // Each instance of SharedMemory is responsible for joining/releasing its
  // reference to the shared memory block. Call std::move() if you want to
  // move a shared memory object.
  SharedMemory(const SharedMemory& other) = delete;
  SharedMemory& operator=(const SharedMemory&) = delete;

  // Wraps around a shared memory block with the given ID.
  SharedMemory(size_t id);

  ~SharedMemory();

  // Creates a shared memory block of a specific size. The size is rounded up
  // to the nearest page size. Flags is a bitfield. if kLazilyAllocated is set,
  // on_page_request must be set.
  static std::unique_ptr<SharedMemory> FromSize(size_t size_in_bytes,
    size_t flags, std::function<void(size_t)> on_page_request = nullptr);

  // Creates another instance of the SharedMemory object that points to the
  // same shared memory.
  SharedMemory Clone() const;

  // Attempts to join the shared memory. This is done automatically if you
  // call any other operations, but you might want to do this manually if you
  // just want to hold onto the shared memory.
  bool Join();

  // Can joiners (not the creator) write to this shared memory buffer?
  bool CanJoinersWrite();

  // Can this procses write to this shared memory buffer?
  bool CanWrite();

  // Is this shared memory lazily allocated?
  bool IsLazilyAllocated();

  // Is this particular page allocated?
  // The can be used by creators of lazily allocated pages to tell if a page
  // needs populating.
  bool IsPageAllocated(size_t offset_in_bytes);

  // Assign page to the shared memory, if we're the creator of the memory buffer.
  // The page is unmapped from its old address and moved into the shared memory.
  // Even if this fails (we're not the creator, of the offset is beyond the end
  // of the buffer), the page is unallocated from the old address.
  //
  // The entire memory page containing the address is moved into the buffer,
  // so it's preferred that you pass PAGE_SIZE aligned addresses.
  void AssignPage(void* page, size_t offset_in_bytes);

  // Returns the ID of the shared memory. Used to identify this shared memory
  // block.
  size_t GetId() const;

  // Returns the size of the shared memory, or 0 if the shared memory is
  // invalid.
  size_t GetSize();

  // Returns a pointer to the shared memory, or nullptr if the shared memory is
  // invalid.
  void* operator*();

  // Returns a pointer to the shared memory, or nullptr if the shared memory is
  // invalid.
  void* operator->();

  // Calls the passed in function if the shared memory is valid, passing in a
  // pointer to the data and the size of the shared memory.
  void Apply(const std::function<void(void*, size_t)>& function);

 private:
  // The unique ID representhing this shared memory buffer on the system.
  size_t shared_memory_id_;

  // Pointer to the raw memory area. This is nulptr if the shared memory is
  // invalid.
  void* ptr_;

  // Size of the shared memory area, in bytes. This is 0 if the shared memory
  // is invalid.
  size_t size_in_bytes_;

  // Flags that the shared memory buffer was created with.
  size_t flags_;

  // Are we the creator of a lazily allocated buffer?
  bool is_creator_of_lazily_allocated_buffer_;

  // The ID of messages coming for page requests. This is only set if we're the creator
  // of a lazily allocated memory buffer.
  size_t on_page_request_message_id_;
};

}  // namespace perception
