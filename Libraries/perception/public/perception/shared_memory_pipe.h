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

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

#include "perception/serialization/serializable.h"
#include "perception/shared_memory.h"
#include "perception/terminal_service.h"
#include "types.h"

namespace perception {

// An inter-process circular byte pipe backed by a single shared memory buffer.
class SharedMemoryPipe : public serialization::Serializable {
 public:
  // Total size in bytes of the shared memory region.
  static constexpr size_t kTotalBytes = 16384;

  // Size in bytes of the control header at the beginning of the region.
  static constexpr size_t kHeaderBytes = 64;

  // Capacity in bytes of the circular data buffer.
  static constexpr size_t kBufferCapacity = kTotalBytes - kHeaderBytes;

  // Byte offset used for the futex lock event.
  static constexpr size_t kLockOffset = 0;

  // Byte offset used for the data-ready event.
  static constexpr size_t kDataReadyOffset = 4;

  // Byte offset used for the space-ready event.
  static constexpr size_t kSpaceReadyOffset = 8;

  // Magic value identifying an initialized SharedMemoryPipe header.
  static constexpr uint32 kMagic = 0x50504950;

  struct Header {
    std::atomic<uint32> lock_state;
    std::atomic<uint32> waiting_readers;
    std::atomic<uint32> waiting_writers;
    std::atomic<uint32> active_readers;
    std::atomic<uint32> active_writers;
    uint32 read_offset;
    uint32 write_offset;
    uint32 buffered_bytes;
    uint32 magic;
    std::atomic<uint32> reserved;
    std::atomic<ProcessId> terminal_process_id;
    std::atomic<MessageId> terminal_service_id;
    uint64 reserved2;
  };
  static_assert(sizeof(Header) == kHeaderBytes);

  SharedMemoryPipe();
  explicit SharedMemoryPipe(size_t shared_memory_id);
  explicit SharedMemoryPipe(SharedMemory&& shared_memory);
  SharedMemoryPipe(SharedMemoryPipe&& other);
  SharedMemoryPipe& operator=(SharedMemoryPipe&& other);
  ~SharedMemoryPipe();

  // Allocates and initializes a new SharedMemoryPipe.
  static std::shared_ptr<SharedMemoryPipe> Create();

  // Wraps an existing SharedMemoryPipe by its shared memory ID.
  static std::shared_ptr<SharedMemoryPipe> FromSharedMemoryId(
      size_t shared_memory_id);

  // Returns whether this pipe wraps a valid initialized shared memory region.
  bool IsValid();

  // Returns the underlying shared memory ID.
  size_t GetId() const;

  // Joins the underlying shared memory region in the current process.
  bool Join();

  // Joins the underlying shared memory region into a child process being
  // created.
  bool JoinChildProcess(ProcessId child_pid);

  // Increments the active reader count.
  void AddReader();

  // Increments the active writer count.
  void AddWriter();

  // Decrements the active reader count and wakes waiting writers on last close.
  void CloseReader();

  // Decrements the active writer count and wakes waiting readers on last close.
  void CloseWriter();

  // Returns whether all writers have closed or disconnected.
  bool IsEof();

  // Returns whether all readers have closed or disconnected.
  bool IsBrokenPipe();

  // Returns the number of unread bytes currently in the buffer.
  size_t BufferedBytes();

  // Returns the number of free bytes available for writing in the buffer.
  size_t FreeSpace();

  // Reads up to `max_size` bytes into `buffer`. Returns bytes read, 0 on EOF,
  // or a negative errno value on error.
  long Read(char* buffer, size_t max_size, bool non_blocking = false);

  // Writes `size` bytes from `data`. Returns bytes written or a negative errno
  // value on error.
  long Write(const char* data, size_t size, bool non_blocking = false);

  // Associates a TerminalService client with this pipe.
  void SetTerminalService(const TerminalService::Client& client);

  // Returns the associated TerminalService client, if any.
  std::optional<TerminalService::Client> GetTerminalService();

  // Returns whether a TerminalService is associated with this pipe.
  bool HasTerminalService();

  // Registers an asynchronous callback invoked when data becomes available.
  void OnDataAvailable(std::function<void()> callback);

  // Registers a custom waiter message ID for poll/select notification.
  void RegisterWaiter(bool is_writer, MessageId message_id);

  // Unregisters a custom waiter message ID.
  void UnregisterWaiter(bool is_writer, MessageId message_id);

  virtual void Serialize(serialization::Serializer& serializer) override;

 private:
  SharedMemory shared_memory_;
  MessageId data_available_msg_id_ = 0;
  std::function<void()> on_data_available_;
  uint32 local_readers_ = 0;
  uint32 local_writers_ = 0;

  Header* GetHeader();
  uint8* GetBuffer();
};

}  // namespace perception
