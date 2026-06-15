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
    case FileDescriptor::DIRECTORY:
    default:
      return WriteDefault(buffers, buffer_count);
  }
}

}  // namespace linux_syscalls
}  // namespace perception
