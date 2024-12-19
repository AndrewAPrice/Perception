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
#pragma once

#ifdef __TEST__
#include <string.h>
#endif  // __TEST__

#include "types.h"

// Copies a string.
void CopyString(const char *source, size_t buffer_size, size_t strlen,
                char *dest);

#ifndef __TEST__
// Compares two strings and returns if they're equal.
bool strcmp(void *a, void *b, size_t count);

// Measures the size of a string.
size_t strlen(const char *str);
#endif

// Measures the size of a string with a maximum length.
size_t strlen_s(const char *str, size_t max);
