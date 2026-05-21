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

#include "kernel_string.h"

#include "object_pools.h"
#include "testing.h"

TEST(StringHelperCopyTest) {
  char dest_buffer[32];

  // Verify CopyString limits copy to buffer size and null-terminates
  CopyString("Hello World!", 10, 12, dest_buffer);
  ASSERT(dest_buffer[9], '\0');  // Room for terminator at buffer_size - 1

  int match = 1;
  const char* expected = "Hello Wor";
  for (int i = 0; i < 10; i++) {
    if (dest_buffer[i] != expected[i]) {
      match = 0;
      break;
    }
  }
  ASSERT(match, 1);
}

TEST(StringHelperLengthTest) {
  const char* text = "Perception";

  // Verify strlen_s fits limits
  ASSERT(strlen_s(text, 5), (size_t)5);
  ASSERT(strlen_s(text, 20), (size_t)10);
}
