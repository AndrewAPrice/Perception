// Copyright 2020 Google LLC
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

#include "linux_syscalls/readv.h"

#include <errno.h>
#include <sys/uio.h>

#include <iostream>

#include "files.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"

using ::permebuf::perception::File;

namespace perception {
namespace linux_syscalls {

long readv(long fd, void* iov, long iovcnt) {
  auto descriptor = GetFileDescriptor(fd);
  if (!descriptor || descriptor->type != FileDescriptor::FILE) {
    errno = EINVAL;
    return -1;
  }

  // Count how many bytes we want to read.
  size_t bytes_to_read = 0;
  for (int io_entry = 0; io_entry < iovcnt; io_entry++) {
    const auto& io = ((const iovec*)iov)[io_entry];
    bytes_to_read += (size_t)io.iov_len;
  }

  // Prune to how many bytes there actually are remaining in the file.
  bytes_to_read = std::min(bytes_to_read, descriptor->file.size_in_bytes -
                                              descriptor->file.offset_in_file);

  if (bytes_to_read == 0) {
    // Nothing to read. Either the file is empty or we've read all that was
    // in the file.
    return 0;
  }

  // Break into page size chunks.
  int num_chunks = (bytes_to_read + kPageSize - 1) / kPageSize;

  // Grab a shared memory buffer that we'll use for communicating with the
  // storage service to copy chunks into.
  auto pooled_shared_memory = kSharedMemoryPool.GetSharedMemory();
  char* chunk_data = (char*)**pooled_shared_memory->shared_memory;

  size_t bytes_read = 0;

  // Read each chunk.
  for (int chunk_to_read = 0; chunk_to_read < num_chunks; chunk_to_read++) {
    size_t bytes_to_read_this_chunk = std::min(kPageSize, bytes_to_read);

    // The start and end of this chunk, in file offset.
    size_t chunk_start = descriptor->file.offset_in_file + bytes_read;
    size_t chunk_end = chunk_start + bytes_to_read_this_chunk;

    // Create the request to send to storage manager.
    File::ReadFileRequest request;
    request.SetOffsetInFile(chunk_start);
    request.SetOffsetInDestinationBuffer(0);
    request.SetBytesToCopy(bytes_to_read_this_chunk);
    request.SetBufferToCopyInto(*pooled_shared_memory->shared_memory);

    // Read the file.
    auto status_or_response =
        descriptor->file.file.CallReadFile(std::move(request));

    if (!status_or_response) {
      std::cout << "Error while reading file." << std::endl;
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
      errno = EINVAL;
      return -1;
    }

    // Copy from the buffer we share with Storage Manager into the buffers
    // passed into readv.
    size_t buffer_start_offset = 0;
    for (int io_entry = 0; io_entry < iovcnt; io_entry++) {
      const auto& io = ((const iovec*)iov)[io_entry];
      size_t buffer_length = (size_t)io.iov_len;

      // The start and end of this buffer, in file offset.
      size_t buffer_start =
          descriptor->file.offset_in_file + buffer_start_offset;
      size_t buffer_end = buffer_start + buffer_length;

      // See if there is an overlap between this chunk and this buffer to
      // copy to.
      if (buffer_start < chunk_end && chunk_start < buffer_end) {
        size_t copy_start = std::max(buffer_start, chunk_start);
        size_t copy_end = std::min(buffer_end, chunk_end);

        size_t bytes_to_copy = copy_end - copy_start;

        size_t offset_in_buffer = copy_start - buffer_start;
        size_t offset_in_chunk = copy_start - chunk_start;

        // Copy from the chunk into the buffer.
        memcpy(&((char*)io.iov_base)[offset_in_buffer],
               &chunk_data[offset_in_chunk], bytes_to_copy);
      }

      buffer_start_offset += buffer_length;
    }

    // Increment how much we've read for this chunk.
    bytes_read += bytes_to_read_this_chunk;
  }

  // Remember how far we've read into this file, so subsequent calls can
  // continue reading the following data in the file.
  descriptor->file.offset_in_file += bytes_read;

  return bytes_read;
}

}  // namespace linux_syscalls
}  // namespace perception
