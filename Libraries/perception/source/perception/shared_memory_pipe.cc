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

#include "perception/shared_memory_pipe.h"

#include <algorithm>
#include <cstring>

#include "perception/messages.h"
#include "perception/shared_memory_futex.h"

namespace perception {
namespace {

// Flag set in Header::reserved when at least one writer has attached.
constexpr uint32 kFlagWriterEverAttached = 1 << 0;

// Flag set in Header::reserved when at least one reader has attached.
constexpr uint32 kFlagReaderEverAttached = 1 << 1;

// Flag set in Header::reserved when all attached writers have closed.
constexpr uint32 kFlagAllWritersClosed = 1 << 2;

// Flag set in Header::reserved when all attached readers have closed.
constexpr uint32 kFlagAllReadersClosed = 1 << 3;

// Flag set in Header::reserved when a TerminalService has been configured.
constexpr uint32 kFlagHasTerminalService = 1 << 4;

// POSIX negative error code for bad file descriptor (-EBADF).
constexpr long kErrorBadFileDescriptor = -9;

// POSIX negative error code for resource temporarily unavailable (-EAGAIN).
constexpr long kErrorAgain = -11;

// POSIX negative error code for invalid argument (-EINVAL).
constexpr long kErrorInvalidArgument = -22;

// POSIX negative error code for broken pipe (-EPIPE).
constexpr long kErrorBrokenPipe = -32;

}  // namespace

SharedMemoryPipe::SharedMemoryPipe() = default;

SharedMemoryPipe::SharedMemoryPipe(size_t shared_memory_id)
    : shared_memory_(shared_memory_id) {}

SharedMemoryPipe::SharedMemoryPipe(SharedMemory&& shared_memory)
    : shared_memory_(std::move(shared_memory)) {}

SharedMemoryPipe::SharedMemoryPipe(SharedMemoryPipe&& other)
    : shared_memory_(std::move(other.shared_memory_)),
      local_readers_(other.local_readers_),
      local_writers_(other.local_writers_) {
  other.OnDataAvailable(nullptr);
  other.local_readers_ = 0;
  other.local_writers_ = 0;
}

SharedMemoryPipe& SharedMemoryPipe::operator=(SharedMemoryPipe&& other) {
  if (this != &other) {
    OnDataAvailable(nullptr);
    other.OnDataAvailable(nullptr);
    shared_memory_ = std::move(other.shared_memory_);
    local_readers_ = other.local_readers_;
    local_writers_ = other.local_writers_;
    other.local_readers_ = 0;
    other.local_writers_ = 0;
  }
  return *this;
}

SharedMemoryPipe::~SharedMemoryPipe() { OnDataAvailable(nullptr); }

std::shared_ptr<SharedMemoryPipe> SharedMemoryPipe::Create() {
  auto shm =
      SharedMemory::FromSize(kTotalBytes, SharedMemory::kJoinersCanWrite);
  if (!shm || shm->GetSize() < kTotalBytes) return nullptr;
  void* ptr = **shm;
  if (ptr == nullptr) return nullptr;
  std::memset(ptr, 0, kTotalBytes);
  auto* header = static_cast<Header*>(ptr);
  header->magic = kMagic;
  return std::make_shared<SharedMemoryPipe>(std::move(*shm));
}

std::shared_ptr<SharedMemoryPipe> SharedMemoryPipe::FromSharedMemoryId(
    size_t shared_memory_id) {
  if (shared_memory_id == 0) return nullptr;
  auto pipe = std::make_shared<SharedMemoryPipe>(shared_memory_id);
  if (!pipe->IsValid()) return nullptr;
  return pipe;
}

bool SharedMemoryPipe::IsValid() { return GetHeader() != nullptr; }

size_t SharedMemoryPipe::GetId() const { return shared_memory_.GetId(); }

bool SharedMemoryPipe::Join() { return shared_memory_.Join() && IsValid(); }

bool SharedMemoryPipe::JoinChildProcess(ProcessId child_pid) {
  return shared_memory_.JoinChildProcess(child_pid, 0);
}

void SharedMemoryPipe::AddReader() {
  Header* header = GetHeader();
  if (header == nullptr) return;
  ++local_readers_;
  header->active_readers.fetch_add(1, std::memory_order_seq_cst);
  header->reserved.fetch_or(kFlagReaderEverAttached, std::memory_order_seq_cst);
  header->reserved.fetch_and(~kFlagAllReadersClosed, std::memory_order_seq_cst);
}

void SharedMemoryPipe::AddWriter() {
  Header* header = GetHeader();
  if (header == nullptr) return;
  ++local_writers_;
  header->active_writers.fetch_add(1, std::memory_order_seq_cst);
  header->reserved.fetch_or(kFlagWriterEverAttached, std::memory_order_seq_cst);
  header->reserved.fetch_and(~kFlagAllWritersClosed, std::memory_order_seq_cst);
}

void SharedMemoryPipe::CloseReader() {
  Header* header = GetHeader();
  if (header == nullptr) return;
  if (local_readers_ > 0) --local_readers_;
  uint32 prev = header->active_readers.fetch_sub(1, std::memory_order_seq_cst);
  if (prev <= 1) {
    if (prev == 0)
      header->active_readers.store(0, std::memory_order_seq_cst);
    header->reserved.fetch_or(kFlagAllReadersClosed, std::memory_order_seq_cst);
    shared_memory_.TriggerEvent(kSpaceReadyOffset);
  }
}

void SharedMemoryPipe::CloseWriter() {
  Header* header = GetHeader();
  if (header == nullptr) return;
  if (local_writers_ > 0) --local_writers_;
  uint32 prev = header->active_writers.fetch_sub(1, std::memory_order_seq_cst);
  if (prev <= 1) {
    if (prev == 0)
      header->active_writers.store(0, std::memory_order_seq_cst);
    header->reserved.fetch_or(kFlagAllWritersClosed, std::memory_order_seq_cst);
    shared_memory_.TriggerEvent(kDataReadyOffset);
  }
}

bool SharedMemoryPipe::IsEof() {
  Header* header = GetHeader();
  if (header == nullptr) return true;
  uint32 flags = header->reserved.load(std::memory_order_seq_cst);
  if ((flags & kFlagAllWritersClosed) != 0) return true;
  if ((flags & kFlagWriterEverAttached) != 0 &&
      header->active_writers.load(std::memory_order_seq_cst) == 0)
    return true;
  if (local_writers_ == 0) {
    SharedMemoryDetails details = shared_memory_.GetDetails();
    if (details.Exists && details.ReferencesCount == 1) return true;
  }
  return false;
}

bool SharedMemoryPipe::IsBrokenPipe() {
  Header* header = GetHeader();
  if (header == nullptr) return true;
  uint32 flags = header->reserved.load(std::memory_order_seq_cst);
  if ((flags & kFlagAllReadersClosed) != 0) return true;
  if ((flags & kFlagReaderEverAttached) != 0 &&
      header->active_readers.load(std::memory_order_seq_cst) == 0)
    return true;
  if (local_readers_ == 0 && local_writers_ > 0) {
    SharedMemoryDetails details = shared_memory_.GetDetails();
    if (details.Exists && details.ReferencesCount == 1) return true;
  }
  return false;
}

size_t SharedMemoryPipe::BufferedBytes() {
  Header* header = GetHeader();
  if (header == nullptr) return 0;
  SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
  size_t bytes = header->buffered_bytes;
  SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);
  return bytes;
}

size_t SharedMemoryPipe::FreeSpace() {
  Header* header = GetHeader();
  if (header == nullptr) return 0;
  SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
  size_t free_bytes = kBufferCapacity - header->buffered_bytes;
  SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);
  return free_bytes;
}

long SharedMemoryPipe::Read(char* buffer, size_t max_size, bool non_blocking) {
  if (max_size == 0) return 0;
  if (buffer == nullptr) return kErrorInvalidArgument;
  Header* header = GetHeader();
  if (header == nullptr) return kErrorBadFileDescriptor;
  uint8* ring = GetBuffer();

  while (true) {
    SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
    if (header->buffered_bytes > 0) {
      size_t to_read =
          std::min(max_size, static_cast<size_t>(header->buffered_bytes));
      size_t first_chunk =
          std::min(to_read, kBufferCapacity - header->read_offset);
      std::memcpy(buffer, ring + header->read_offset, first_chunk);
      if (to_read > first_chunk)
        std::memcpy(buffer + first_chunk, ring, to_read - first_chunk);
      header->read_offset =
          static_cast<uint32>((header->read_offset + to_read) % kBufferCapacity);
      header->buffered_bytes -= static_cast<uint32>(to_read);
      SharedMemoryFutex::Unlock(shared_memory_, kLockOffset,
                                header->lock_state);

      if (header->waiting_writers.load(std::memory_order_seq_cst) > 0)
        shared_memory_.TriggerEvent(kSpaceReadyOffset);
      return static_cast<long>(to_read);
    }
    SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);

    if (IsEof()) return 0;
    if (non_blocking) return kErrorAgain;

    MessageId msg_id = GenerateUniqueMessageId();
    RegisterWakeUpHandler(msg_id);
    header->waiting_readers.fetch_add(1, std::memory_order_seq_cst);
    shared_memory_.RegisterEvent(kDataReadyOffset, msg_id);

    SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
    uint32 current_buffered = header->buffered_bytes;
    SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);

    if (current_buffered == 0 && !IsEof()) {
      ProcessId sender = 0;
      MessageData message_data = {};
      SleepAndGetRawMessage(msg_id, sender, message_data);
    } else {
      UnregisterMessageHandler(msg_id);
    }

    shared_memory_.UnregisterEvent(kDataReadyOffset);
    header->waiting_readers.fetch_sub(1, std::memory_order_seq_cst);
    if (data_available_msg_id_ != 0)
      shared_memory_.RegisterEvent(kDataReadyOffset, data_available_msg_id_);
  }
}

long SharedMemoryPipe::Write(const char* data, size_t size, bool non_blocking) {
  if (size == 0) return 0;
  if (data == nullptr) return kErrorInvalidArgument;
  Header* header = GetHeader();
  if (header == nullptr) return kErrorBadFileDescriptor;
  uint8* ring = GetBuffer();

  size_t total_written = 0;
  while (total_written < size) {
    if (IsBrokenPipe())
      return total_written > 0 ? static_cast<long>(total_written)
                               : kErrorBrokenPipe;

    SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
    size_t free_space = kBufferCapacity - header->buffered_bytes;
    if (free_space > 0) {
      size_t to_write = std::min(size - total_written, free_space);
      size_t first_chunk =
          std::min(to_write, kBufferCapacity - header->write_offset);
      std::memcpy(ring + header->write_offset, data + total_written,
                  first_chunk);
      if (to_write > first_chunk)
        std::memcpy(ring, data + total_written + first_chunk,
                    to_write - first_chunk);
      header->write_offset = static_cast<uint32>(
          (header->write_offset + to_write) % kBufferCapacity);
      header->buffered_bytes += static_cast<uint32>(to_write);
      total_written += to_write;
      SharedMemoryFutex::Unlock(shared_memory_, kLockOffset,
                                header->lock_state);

      if (header->waiting_readers.load(std::memory_order_seq_cst) > 0)
        shared_memory_.TriggerEvent(kDataReadyOffset);
      if (total_written == size || non_blocking)
        return static_cast<long>(total_written);
      continue;
    }
    SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);

    if (non_blocking)
      return total_written > 0 ? static_cast<long>(total_written) : kErrorAgain;

    MessageId msg_id = GenerateUniqueMessageId();
    RegisterWakeUpHandler(msg_id);
    header->waiting_writers.fetch_add(1, std::memory_order_seq_cst);
    shared_memory_.RegisterEvent(kSpaceReadyOffset, msg_id);

    SharedMemoryFutex::Lock(shared_memory_, kLockOffset, header->lock_state);
    uint32 current_buffered = header->buffered_bytes;
    SharedMemoryFutex::Unlock(shared_memory_, kLockOffset, header->lock_state);

    if (current_buffered == kBufferCapacity && !IsBrokenPipe()) {
      ProcessId sender = 0;
      MessageData message_data = {};
      SleepAndGetRawMessage(msg_id, sender, message_data);
    } else {
      UnregisterMessageHandler(msg_id);
    }

    shared_memory_.UnregisterEvent(kSpaceReadyOffset);
    header->waiting_writers.fetch_sub(1, std::memory_order_seq_cst);
  }
  return static_cast<long>(total_written);
}

void SharedMemoryPipe::SetTerminalService(
    const TerminalService::Client& client) {
  Header* header = GetHeader();
  if (header == nullptr) return;
  header->terminal_process_id.store(client.ServerProcessId(),
                                    std::memory_order_seq_cst);
  header->terminal_service_id.store(client.ServiceId(),
                                    std::memory_order_seq_cst);
  if (client.ServerProcessId() != 0 || client.ServiceId() != 0)
    header->reserved.fetch_or(kFlagHasTerminalService,
                              std::memory_order_seq_cst);
  else
    header->reserved.fetch_and(~kFlagHasTerminalService,
                               std::memory_order_seq_cst);
}

std::optional<TerminalService::Client> SharedMemoryPipe::GetTerminalService() {
  Header* header = GetHeader();
  if (header == nullptr) return std::nullopt;
  ProcessId pid = header->terminal_process_id.load(std::memory_order_seq_cst);
  MessageId sid = header->terminal_service_id.load(std::memory_order_seq_cst);
  uint32 flags = header->reserved.load(std::memory_order_seq_cst);
  if (pid == 0 && sid == 0 && (flags & kFlagHasTerminalService) == 0)
    return std::nullopt;
  return TerminalService::Client(pid, sid);
}

bool SharedMemoryPipe::HasTerminalService() {
  return GetTerminalService().has_value();
}

void SharedMemoryPipe::OnDataAvailable(std::function<void()> callback) {
  Header* header = GetHeader();
  if (data_available_msg_id_ != 0) {
    UnregisterMessageHandler(data_available_msg_id_);
    shared_memory_.UnregisterEvent(kDataReadyOffset);
    if (header != nullptr)
      header->waiting_readers.fetch_sub(1, std::memory_order_seq_cst);
    data_available_msg_id_ = 0;
  }

  on_data_available_ = std::move(callback);
  if (!on_data_available_ || header == nullptr) return;

  data_available_msg_id_ = GenerateUniqueMessageId();
  header->waiting_readers.fetch_add(1, std::memory_order_seq_cst);
  RegisterMessageHandler(
      data_available_msg_id_, [this](ProcessId, const MessageData&) {
        if (data_available_msg_id_ != 0)
          shared_memory_.RegisterEvent(kDataReadyOffset,
                                       data_available_msg_id_);
        if (on_data_available_) on_data_available_();
      });
  shared_memory_.RegisterEvent(kDataReadyOffset, data_available_msg_id_);
  if (BufferedBytes() > 0) on_data_available_();
}

void SharedMemoryPipe::RegisterWaiter(bool is_writer, MessageId message_id) {
  Header* header = GetHeader();
  if (header == nullptr) return;
  if (is_writer) {
    header->waiting_writers.fetch_add(1, std::memory_order_seq_cst);
    shared_memory_.RegisterEvent(kSpaceReadyOffset, message_id);
  } else {
    header->waiting_readers.fetch_add(1, std::memory_order_seq_cst);
    shared_memory_.RegisterEvent(kDataReadyOffset, message_id);
  }
}

void SharedMemoryPipe::UnregisterWaiter(bool is_writer, MessageId message_id) {
  (void)message_id;
  Header* header = GetHeader();
  if (header == nullptr) return;
  if (is_writer) {
    shared_memory_.UnregisterEvent(kSpaceReadyOffset);
    header->waiting_writers.fetch_sub(1, std::memory_order_seq_cst);
  } else {
    shared_memory_.UnregisterEvent(kDataReadyOffset);
    header->waiting_readers.fetch_sub(1, std::memory_order_seq_cst);
    if (data_available_msg_id_ != 0)
      shared_memory_.RegisterEvent(kDataReadyOffset, data_available_msg_id_);
  }
}

void SharedMemoryPipe::Serialize(serialization::Serializer& serializer) {
  shared_memory_.Serialize(serializer);
}

SharedMemoryPipe::Header* SharedMemoryPipe::GetHeader() {
  if (shared_memory_.GetSize() < kTotalBytes) return nullptr;
  void* ptr = *shared_memory_;
  if (ptr == nullptr) return nullptr;
  auto* header = static_cast<Header*>(ptr);
  if (header->magic != kMagic) return nullptr;
  return header;
}

uint8* SharedMemoryPipe::GetBuffer() {
  void* ptr = *shared_memory_;
  if (ptr == nullptr) return nullptr;
  return static_cast<uint8*>(ptr) + kHeaderBytes;
}

}  // namespace perception
