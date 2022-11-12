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
#include "shared_memory_pool.h"
#include "virtual_file_system.h"

using ::perception::AllocateMemoryPages;
using ::perception::Defer;
using ::perception::kPageSize;
using ::perception::ProcessId;
using ::perception::SharedMemory;
using ReadFileRequest = ::permebuf::perception::File::ReadFileRequest;
using MMF = ::permebuf::perception::MemoryMappedFile;

MemoryMappedFile::MemoryMappedFile(std::unique_ptr<File> file,
                                   size_t length_of_file,
                                   ProcessId allowed_process)
    : file_(std::move(file)),
      allowed_process_(allowed_process),
      length_of_file_(length_of_file) {
  if (length_of_file > 0) {
    buffer_ = SharedMemory::FromSize(
        length_of_file, SharedMemory::kLazilyAllocated,
        [this](size_t offset_of_page) { ReadInPageChunk(offset_of_page); });
    buffer_->Join();
  }
}

void MemoryMappedFile::HandleCloseFile(ProcessId sender,
                                       const MMF::CloseFileMessage&) {
  if (sender != allowed_process_) return;

  std::scoped_lock lock(mutex_);

  Defer([sender, this]() { CloseMemoryMappedFile(sender, this); });
}

void MemoryMappedFile::ReadInPageChunk(size_t offset_of_page) {
  std::scoped_lock lock(mutex_);

  if (buffer_->IsPageAllocated(offset_of_page)) {
    return;  // This page is already allocated, so nothing to do.
  }

  // Read the page in from the file.
  auto temp_buffer = kSharedMemoryPool.GetSharedMemory();
  ReadFileRequest request;
  request.SetBufferToCopyInto(*temp_buffer->shared_memory);
  request.SetOffsetInFile(offset_of_page);
  size_t remaining_bytes_in_file = length_of_file_ - offset_of_page;
  size_t bytes_to_copy =
      kPageSize > remaining_bytes_in_file ? remaining_bytes_in_file : kPageSize;
  request.SetBytesToCopy(bytes_to_copy);
  auto read_status = file_->HandleReadFile(allowed_process_, request);

  // Create a new page to copy this temporary page into.
  void* new_page = AllocateMemoryPages(1);
  if (read_status.Ok()) {
    // An optimization would be to modify the kernel so we could the ownership
    // of the temporary buffer's page to this process, thereby avoiding this
    // copying.
    memcpy(new_page, **temp_buffer->shared_memory, kPageSize);

    if (bytes_to_copy < kPageSize) {
      // We didn't copy an entire page, so we'll clear the end of the page.
      memset((void*)((size_t)new_page + bytes_to_copy), 0,
             kPageSize - bytes_to_copy);
    }
  } else {
    memset(new_page, 0, kPageSize);
  }

  kSharedMemoryPool.ReleaseSharedMemory(std::move(temp_buffer));

  // Assigns the page, that's now filled with the file's contents, into the
  // shared buffer.
  buffer_->AssignPage(new_page, offset_of_page);
}

SharedMemory& MemoryMappedFile::GetBuffer() { return *buffer_; }
