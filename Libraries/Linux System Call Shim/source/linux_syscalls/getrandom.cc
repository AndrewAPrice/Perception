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

#include "linux_syscalls/getrandom.h"

#include <algorithm>
#include <cstring>
#include <errno.h>

#include "perception/random.h"

namespace perception {
namespace linux_syscalls {
namespace {

uint64_t xorshift64() {
  static thread_local uint64_t state = 0;
  if (state == 0) {
    state = ::perception::RandomNumber();
    if (state == 0) state = 1;
  }
  uint64_t x = state;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  state = x;
  return x * 0x2545F4914F6CDD1DULL;
}

}  // namespace

long getrandom(void* buf, size_t buflen, unsigned int flags) {
  if (buf == nullptr) return -EFAULT;

  unsigned char* byte_ptr = reinterpret_cast<unsigned char*>(buf);
  size_t bytes_written = 0;

  while (bytes_written < buflen) {
    uint64_t random_val = xorshift64();
    size_t chunk_size = std::min(buflen - bytes_written, sizeof(uint64_t));
    std::memcpy(byte_ptr + bytes_written, &random_val, chunk_size);
    bytes_written += chunk_size;
  }

  return (long)buflen;
}

}  // namespace linux_syscalls
}  // namespace perception
