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

#include "perception/shared_memory.h"

#ifndef PERCEPTION
#include <map>
#endif

#include "perception/memory.h"
#include "perception/messages.h"

namespace perception {
namespace {

#ifndef PERCEPTION
// When we are not building for running on the Perception OS, we simulated
// shared memory in the process's local memory.

// Represents a simulated shared memory block.
struct SharedMemoryBlock {
  void* data;
  size_t size_in_pages;
  size_t references;
};

// The last unique buffer ID - where the first allocated buffer starts from 1
// because 0 means invalid buffer.
size_t last_unique_shared_buffer_id = 0;

// A map of the shared memory buffer IDs to the allocated shared memory blocks.
std::map<size_t, SharedMemoryBlock> shared_memory_blocks;

#endif

// Performs the system call to create a region of shared memory.
void CreateSharedMemory(size_t size_in_pages, size_t flags,
                        size_t on_page_request_message_id, size_t& id,
                        void*& ptr) {
#if PERCEPTION
  volatile register size_t syscall_num asm("rdi") = 42;
  volatile register size_t size_r asm("rax") = size_in_pages;
  volatile register size_t flags_r asm("rbx") = flags;
  volatile register size_t on_page_request_message_id_r asm("rdx") =
      on_page_request_message_id;
  volatile register size_t id_r asm("rax");
  volatile register size_t address_r asm("rbx");

  __asm__ __volatile__("syscall\n"
                       : "=r"(id_r), "=r"(address_r)
                       : "r"(syscall_num), "r"(size_r), "r"(flags_r),
                         "r"(on_page_request_message_id_r)
                       : "rcx", "r11");
  id = id_r;
  ptr = (void*)address_r;
#else
  id = last_unique_shared_buffer_id++;

  SharedMemoryBlock shared_memory_block;
  ptr = malloc(size_in_pages * kPageSize);
  shared_memory_block.data = ptr;
  shared_memory_block.size_in_pages = size_in_pages;
  shared_memory_block.references = 0;
  shared_memory_blocks[id] = shared_memory_block;
#endif
}

// Performs the system call to join a region of shared memory.
void JoinSharedMemory(size_t id, void*& ptr, size_t& size_in_pages,
                      size_t& flags) {
#if PERCEPTION
  volatile register size_t syscall_num asm("rdi") = 43;
  volatile register size_t id_r asm("rax") = id;
  volatile register size_t size_r asm("rax");
  volatile register size_t address_r asm("rbx");
  volatile register size_t flags_r asm("rdx");

  __asm__ __volatile__("syscall\n"
                       : "=r"(size_r), "=r"(address_r), "=r"(flags_r)
                       : "r"(syscall_num), "r"(id_r)
                       : "rcx", "r11");
  ptr = (void*)address_r;
  size_in_pages = size_r;
  flags = flags_r;
#else
  // Find the shared memory block.
  auto shared_memory_block_itr = shared_memory_blocks.find(id);
  if (shared_memory_block_itr == shared_memory_blocks.end()) {
    // Can't find this memory block.
    ptr = nullptr;
    size_in_pages = 0;
    return;
  }

  // Increment the references and return a pointer to the data.
  shared_memory_block_itr->second.references++;
  ptr = shared_memory_block_itr->second.data;
  size_in_pages = shared_memory_block_itr->second.size_in_pages;
  flags = kJoinersCanWrite;
#endif
}

// Performs the system call to release a region of shared memory.
void ReleaseSharedMemory(size_t id) {
#if PERCEPTION
  volatile register size_t syscall_num asm("rdi") = 44;
  volatile register size_t id_r asm("rax") = id;

  __asm__ __volatile__("syscall\n" ::"r"(syscall_num), "r"(id_r)
                       : "rcx", "r11");
#else
  // Find the shared memory block.
  auto shared_memory_block_itr = shared_memory_blocks.find(id);
  if (shared_memory_block_itr == shared_memory_blocks.end())
    // Can't find this memory block.
    return;

  // Decerement the references.
  shared_memory_block_itr->second.references--;
  if (shared_memory_block_itr->second.references == 0) {
    // There are no more references to this memory block so we can
    // unallocate the memory.
    free(shared_memory_block_itr->second.data);
    shared_memory_blocks.erase(shared_memory_block_itr);
  }
#endif
}

}  // namespace

SharedMemory::SharedMemory()
    : shared_memory_id_(0),
      ptr_(nullptr),
      size_in_bytes_(0),
      is_creator_of_lazily_allocated_buffer_(false),
      flags_(0),
      on_page_request_message_id_(0) {}

// Wraps around a shared memory block with the given ID.
SharedMemory::SharedMemory(size_t id)
    : shared_memory_id_(id),
      ptr_(nullptr),
      size_in_bytes_(0),
      is_creator_of_lazily_allocated_buffer_(false),
      flags_(0),
      on_page_request_message_id_(0) {}

SharedMemory::~SharedMemory() {
  if (size_in_bytes_ != 0) ReleaseSharedMemory(shared_memory_id_);
  if (is_creator_of_lazily_allocated_buffer_)
    UnregisterMessageHandler(on_page_request_message_id_);
}

SharedMemory::SharedMemory(SharedMemory&& other) {
  shared_memory_id_ = other.shared_memory_id_;
  ptr_ = other.ptr_;
  size_in_bytes_ = other.size_in_bytes_;
  flags_ = other.flags_;
  is_creator_of_lazily_allocated_buffer_ =
      other.is_creator_of_lazily_allocated_buffer_;
  on_page_request_message_id_ = other.on_page_request_message_id_;

  other.shared_memory_id_ = 0;
  other.ptr_ = nullptr;
  other.size_in_bytes_ = 0;
  other.flags_ = 0;
  other.is_creator_of_lazily_allocated_buffer_ = false;
  other.on_page_request_message_id_ = 0;
}

SharedMemory& SharedMemory::operator=(SharedMemory&& other) {
  shared_memory_id_ = other.shared_memory_id_;
  ptr_ = other.ptr_;
  size_in_bytes_ = other.size_in_bytes_;
  flags_ = other.flags_;
  is_creator_of_lazily_allocated_buffer_ =
      other.is_creator_of_lazily_allocated_buffer_;
  on_page_request_message_id_ = other.on_page_request_message_id_;

  other.shared_memory_id_ = 0;
  other.ptr_ = nullptr;
  other.size_in_bytes_ = 0;
  other.flags_ = 0;
  other.is_creator_of_lazily_allocated_buffer_ = false;
  return *this;
}

// Creates a shared memory block of a specific size. The size is rounded up
// to the nearest page size.
std::unique_ptr<SharedMemory> SharedMemory::FromSize(
    size_t size_in_bytes, size_t flags,
    std::function<void(size_t)> on_page_request) {
  size_t size_in_pages = (size_in_bytes + kPageSize - 1) / kPageSize;
  if (size_in_pages == 0)
    // Shared memory is empty.
    return std::make_unique<SharedMemory>(0);

  bool is_lazily_allocated = (flags & kLazilyAllocated) != 0;

  MessageId on_page_request_message_id = 0;
  if (is_lazily_allocated) {
    // If we're lazily allocated we need to set up the handler for page
    // requests.
    on_page_request_message_id = GenerateUniqueMessageId();
    RegisterMessageHandler(on_page_request_message_id,
                           [on_page_request](ProcessId, size_t a1, size_t,
                                             size_t, size_t, size_t) {
                             if (on_page_request) on_page_request(a1);
                           });
  }

  size_t id = 0;
  void* ptr = nullptr;
  CreateSharedMemory(size_in_pages, flags, on_page_request_message_id, id, ptr);

  if (id == 0) {
    // Could not create the shared memory.
    if (is_lazily_allocated)
      UnregisterMessageHandler(on_page_request_message_id);
    return std::make_unique<SharedMemory>(0);
  }

  // We've created and allocated a shared memory, so now let's wrap it in a
  // SharedMemory object.
  auto shared_memory = std::make_unique<SharedMemory>(id);
  shared_memory->ptr_ = ptr;
  shared_memory->size_in_bytes_ = size_in_pages * kPageSize;
  shared_memory->flags_ = flags;
  shared_memory->is_creator_of_lazily_allocated_buffer_ = is_lazily_allocated;
  shared_memory->on_page_request_message_id_ = on_page_request_message_id;
  return shared_memory;
}

// Creates another instance of the SharedMemory object that points to the
// same shared memory.
SharedMemory SharedMemory::Clone() const {
  return SharedMemory(shared_memory_id_);
}

bool SharedMemory::operator==(const SharedMemory& other) const {
  return shared_memory_id_ == other.shared_memory_id_;
}

// Attempts to join the shared memory. This is done automatically if you
// call any other operations, but you might want to do this manually if you
// just want to hold onto the shared memory.
bool SharedMemory::Join() {
  if (size_in_bytes_ > 0)
    // We have already aquired the shared memory.
    return true;

  if (shared_memory_id_ == 0)
    // Invalid SharedMemory object.
    return false;

  size_t size_in_pages = 0;

  JoinSharedMemory(shared_memory_id_, ptr_, size_in_pages, flags_);
  size_in_bytes_ = size_in_pages * kPageSize;

  return size_in_bytes_ > 0;
}

bool SharedMemory::CanJoinersWrite() {
  (void)Join();
  return (flags_ & kJoinersCanWrite) != 0;
}

bool SharedMemory::CanWrite() {
  return CanJoinersWrite() || is_creator_of_lazily_allocated_buffer_;
}

// Is this shared memory lazily allocated?
bool SharedMemory::IsLazilyAllocated() {
  (void)Join();
  return (flags_ & kLazilyAllocated) != 0;
}

// Is this particular page allocated?
// The can be used by creators of lazily allocated pages to tell if a page
// needs populating.
bool SharedMemory::IsPageAllocated(size_t offset_in_bytes) {
  if (offset_in_bytes >= GetSize())
    return false;  // Beyond the end of the shared memory.

  if (!IsLazilyAllocated())
    return true;  // Not lazily allocated, so all memory is allocated.

  volatile register size_t syscall_num asm("rdi") = 46;
  volatile register size_t id_r asm("rax") = shared_memory_id_;
  volatile register size_t offset_r asm("rbx") = offset_in_bytes;
  volatile register size_t is_allocated_r asm("rax");

  __asm__ __volatile__("syscall\n"
                       : "=r"(is_allocated_r)
                       : "r"(syscall_num), "r"(id_r), "r"(offset_r)
                       : "rcx", "r11");

  return is_allocated_r == 1;
}

void SharedMemory::AssignPage(void* page, size_t offset_in_bytes) {
  volatile register size_t syscall_num asm("rdi") = 45;
  volatile register size_t id_r asm("rax") = shared_memory_id_;
  volatile register size_t offset_in_shared_memory_r asm("rbx") =
      offset_in_bytes;
  volatile register size_t page_r asm("rdx") = (size_t)page;

  __asm__ __volatile__("syscall\n"
                       :
                       : "r"(syscall_num), "r"(id_r),
                         "r"(offset_in_shared_memory_r), "r"(page_r)
                       : "rcx", "r11");
}

// Returns the ID of the shared memory. Used to identify this shared memory
// block.
size_t SharedMemory::GetId() const { return shared_memory_id_; }

// Returns the size of the shared memory, or 0 if the shared memory is
// invalid.
size_t SharedMemory::GetSize() {
  (void)Join();
  return size_in_bytes_;
}

// Returns a pointer to the shared memory, or nullptr if the shared memory is
// invalid.
void* SharedMemory::operator*() {
  (void)Join();
  return ptr_;
}

// Returns a pointer to the shared memory, or nullptr if the shared memory is
// invalid.
void* SharedMemory::operator->() {
  (void)Join();
  return ptr_;
}

// Returns a pointer to a specific offset in the shared memory, or nullptr if
// the shared memory is invalid.
void* SharedMemory::operator[](size_t offset) {
  (void)Join();
  if (ptr_ == nullptr) return nullptr;
  if (offset >= size_in_bytes_) return nullptr;
  return (void*)((size_t)ptr_ + offset);
}

// Returns a pointer to a specific offset in the shared memory, or nullptr if
// the shared memory is invalid, or there is not enough space to fit the size in
// after this offset.
void* SharedMemory::GetRangeAtOffset(size_t offset, size_t size) {
  (void)Join();
  if (ptr_ == nullptr) return nullptr;
  if (offset + size >= size_in_bytes_) return nullptr;
  return (void*)((size_t)ptr_ + offset);
}

// Calls the passed in function if the shared memory is valid, passing in a
// pointer to the data and the size of the shared memory.
void SharedMemory::Apply(const std::function<void(void*, size_t)>& function) {
  (void)Join();
  if (size_in_bytes_ > 0) function(ptr_, size_in_bytes_);
}

}  // namespace perception
