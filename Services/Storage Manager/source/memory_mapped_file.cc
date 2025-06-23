// Copyright 2022 Google LLC
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

#include "memory_mapped_file.h"

#include "perception/memory.h"
#include "perception/processes.h"
#include "shared_memory_pool.h"
#include "virtual_file_system.h"

using ::perception::AllocateMemoryPages;
using ::perception::Defer;
using ::perception::GetProcessId;
using ::perception::
    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest;
using ::perception::kPageSize;
using ::perception::ProcessId;
using ::perception::ReadFileRequest;
using ::perception::RequestWithFilePath;
using ::perception::SharedMemory;
using ::perception::Status;

namespace {

// Rounds a size down to the nearest page aligned size, but never below the size
// of a single page.
size_t RoundDownToPageAlignSize(size_t size) {
  if (size < kPageSize) size = kPageSize;
  return (size / kPageSize) * kPageSize;
}

}  // namespace

MemoryMappedFile::MemoryMappedFile(std::unique_ptr<File> file,
                                   size_t length_of_file,
                                   size_t optimal_operation_size,
                                   ProcessId allowed_process)
    : file_(std::move(file)),
      allowed_process_(allowed_process),
      optimal_operation_size_(RoundDownToPageAlignSize(optimal_operation_size)),
      length_of_file_(length_of_file),
      close_after_all_operations_(false),
      is_closed_(false),
      running_operations_(0) {
  if (length_of_file > 0) {
    buffer_ =
        SharedMemory::FromSize(length_of_file, SharedMemory::kLazilyAllocated,
                               [this](size_t offset_of_page) {
                                 if (is_closed_) return;
                                 running_operations_++;
                                 ReadInPageChunk(offset_of_page);
                                 running_operations_--;
                                 MaybeCloseIfUnlocked();
                               });
    buffer_->GrantPermissionToLazilyAllocatePage(GetProcessId());

    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest
        grant_request;
    grant_request.buffer = buffer_;
    (void)file_->GrantStorageDevicePermissionToAllocateSharedMemoryPages(
        grant_request, allowed_process_);

    buffer_->Join();
  }
}

Status MemoryMappedFile::Close(ProcessId sender) {
  if (sender != allowed_process_) return Status::NOT_ALLOWED;

  if (running_operations_ == 0) {
    CloseFile();
  } else {
    close_after_all_operations_ = true;
  }

  return Status::OK;
}

void MemoryMappedFile::ReadInPageChunk(size_t offset_of_page) {
  std::scoped_lock lock(mutex_);

  // Round the page offset down.
  offset_of_page =
      (offset_of_page / optimal_operation_size_) * optimal_operation_size_;

  if (buffer_->IsPageAllocated(offset_of_page)) {
    return;  // This page is already allocated, so nothing to do.
  }

  // Read the page in from the file.
  ReadFileRequest request;
  request.buffer_to_copy_into = buffer_;
  request.offset_in_file = offset_of_page;
  request.offset_in_destination_buffer = offset_of_page;
  size_t remaining_bytes_in_file = length_of_file_ - offset_of_page;
  size_t bytes_to_copy =
      std::min(optimal_operation_size_, remaining_bytes_in_file);
  request.bytes_to_copy = bytes_to_copy;

  auto read_status = file_->Read(request, allowed_process_);
  if (read_status != Status::OK) {
    size_t first_page = offset_of_page;
    size_t last_page = first_page + (bytes_to_copy - 1) / kPageSize * kPageSize;
    for (size_t page = first_page; page <= last_page; page++) {
      // Create a new page to copy this temporary page into.
      void* new_page = AllocateMemoryPages(1);
      memset(new_page, 0, kPageSize);
      buffer_->AssignPage(new_page, page * kPageSize);
    }
  }
}

std::shared_ptr<SharedMemory> MemoryMappedFile::GetBuffer() { return buffer_; }

void MemoryMappedFile::MaybeCloseIfUnlocked() {
  if (close_after_all_operations_ && running_operations_ == 0) {
    CloseFile();
  }
}

// Closes the file.
void MemoryMappedFile::CloseFile() {
  if (is_closed_) return;
  is_closed_ = true;
  ProcessId owner = allowed_process_;
  Defer([owner, this]() { CloseMemoryMappedFile(owner, this); });
}
