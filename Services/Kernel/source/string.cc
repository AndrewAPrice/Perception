// Copyright 2024 Google LLC
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

#include "string.h"

#include "memory.h"

void CopyString(const char *source, size_t buffer_size, size_t strlen,
                char *dest) {
  // Leave room for a null terminator.
  if (strlen > buffer_size - 1) {
    strlen = buffer_size - 1;
  }

  memcpy(dest, source, strlen);

  dest += strlen;
  memset(dest, '\0', buffer_size - strlen);
}

#ifndef __TEST__
bool strcmp(void *a, void *b, size_t count) {
  unsigned char *ac = (unsigned char *)a;
  unsigned char *bc = (unsigned char *)b;

  while (count > 0) {
    if (*ac != *bc) return true;
    ac++;
    bc++;
    count--;
  }

  return false;
}

size_t strlen(const char *str) {
  size_t count = 0;
  while (*str) {
    count++;
    str++;
  }

  return count;
}
#endif

size_t strlen_s(const char *str, size_t max_size) {
  size_t count = 0;
  while (*str && count < max_size) {
    count++;
    str++;
  }

  return count;
}
