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

#include "linux_syscalls/writev.h"

#include <errno.h>

#include "files.h"
#include "perception/debug.h"

namespace perception {
namespace linux_syscalls {

using ::perception::network::SendRequest;

namespace {

long WriteSocket(const std::shared_ptr<FileDescriptor>& descriptor,
                 struct iovec* buffers, long buffer_count) {
  std::string data_to_send = "";
  for (size_t i = 0; i < buffer_count; i++) {
    const auto& buffer = buffers[i];
    size_t to_append =
        std::min((size_t)buffer.iov_len, 4096 - data_to_send.length());
    data_to_send.append((const char*)buffer.iov_base, to_append);
    if (data_to_send.length() >= 4096) break;
  }

  SendRequest request;
  request.data = data_to_send;
  auto status = descriptor->socket.socket.Send(request);
  if (status != Status::OK) {
    return -ECONNRESET;
  }

  return data_to_send.length();
}

long WriteFile(const std::shared_ptr<FileDescriptor>& descriptor,
               struct iovec* buffers, long buffer_count) {
  if (buffer_count < 0) {
    errno = EINVAL;
    return -1;
  }

  // Count how many bytes to write.
  size_t bytes_to_write = 0;
  for (int io_entry = 0; io_entry < buffer_count; io_entry++) {
    const auto& io = buffers[io_entry];
    if ((ssize_t)io.iov_len < 0) {
      errno = EINVAL;
      return -1;
    }
    bytes_to_write += (size_t)io.iov_len;
  }

  if (bytes_to_write == 0) return 0;

  // Break into page size chunks.
  int num_chunks = (bytes_to_write + kPageSize - 1) / kPageSize;

  // Grab a shared memory buffer used to communicate with the
  // storage service to copy chunks into.
  auto pooled_shared_memory = kSharedMemoryPool.GetSharedMemory();
  char* chunk_data = (char*)**pooled_shared_memory->shared_memory;

  size_t bytes_written = 0;
  size_t remaining_bytes = bytes_to_write;

  for (int chunk_to_write = 0; chunk_to_write < num_chunks; chunk_to_write++) {
    size_t bytes_to_write_this_chunk = std::min(kPageSize, remaining_bytes);

    // Copy from the uio iovec buffers into the shared memory chunk buffer.
    size_t buffer_start_offset = 0;
    size_t chunk_start = descriptor->file.offset_in_file + bytes_written;
    size_t chunk_end = chunk_start + bytes_to_write_this_chunk;

    for (int io_entry = 0; io_entry < buffer_count; io_entry++) {
      const auto& io = buffers[io_entry];
      size_t buffer_length = (size_t)io.iov_len;

      size_t buffer_start =
          descriptor->file.offset_in_file + buffer_start_offset;
      size_t buffer_end = buffer_start + buffer_length;

      // See if there is an overlap between this chunk and this buffer.
      if (buffer_start < chunk_end && chunk_start < buffer_end) {
        size_t copy_start = std::max(buffer_start, chunk_start);
        size_t copy_end = std::min(buffer_end, chunk_end);

        size_t bytes_to_copy = copy_end - copy_start;

        size_t offset_in_buffer = copy_start - buffer_start;
        size_t offset_in_chunk = copy_start - chunk_start;

        // Copy into the chunk.
        std::memcpy(&chunk_data[offset_in_chunk],
                    &((char*)io.iov_base)[offset_in_buffer], bytes_to_copy);
      }

      buffer_start_offset += buffer_length;
    }

    // Call Write RPC
    WriteFileRequest request;
    request.offset_in_file = chunk_start;
    request.bytes_to_copy = bytes_to_write_this_chunk;
    request.buffer_to_copy_from = pooled_shared_memory->shared_memory;

    auto status = descriptor->file.file.Write(request);
    if (status != Status::OK) {
      kSharedMemoryPool.ReleaseSharedMemory(pooled_shared_memory);
      errno = EINVAL;
      return -1;
    }

    bytes_written += bytes_to_write_this_chunk;
    remaining_bytes -= bytes_to_write_this_chunk;
  }

  kSharedMemoryPool.ReleaseSharedMemory(pooled_shared_memory);

  // Update offset and file size
  descriptor->file.offset_in_file += bytes_written;
  descriptor->file.size_in_bytes =
      std::max(descriptor->file.size_in_bytes, descriptor->file.offset_in_file);

  return bytes_written;
}

long WriteDefault(struct iovec* buffers, long buffer_count) {
  size_t bytes_written = 0;
  for (size_t i = 0; i < buffer_count; i++) {
    const auto& buffer = buffers[i];

    char* c = (char*)buffer.iov_base;
    for (size_t j = 0; j < buffer.iov_len; j++, c++, bytes_written++)
      DebugPrinterSingleton << *c;
  }
  return bytes_written;
}

}  // namespace

long writev(long file_descriptor, struct iovec* buffers, long buffer_count) {
  auto descriptor = GetFileDescriptor(file_descriptor);
  if (!descriptor) {
    return WriteDefault(buffers, buffer_count);
  }

  switch (descriptor->type) {
    case FileDescriptor::SOCKET:
      return WriteSocket(descriptor, buffers, buffer_count);
    case FileDescriptor::FILE:
      return WriteFile(descriptor, buffers, buffer_count);
    case FileDescriptor::DIRECTORY:
    default:
      return WriteDefault(buffers, buffer_count);
  }
}

}  // namespace linux_syscalls
}  // namespace perception
// force rebuild 2


