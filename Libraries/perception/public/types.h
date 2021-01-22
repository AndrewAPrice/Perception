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

#pragma once

#include <stddef.h>

#ifndef __cplusplus
typedef unsigned char bool;
#endif

typedef unsigned long long int uint64;
typedef unsigned int uint32;
typedef unsigned short uint16;
typedef unsigned char uint8;

typedef signed long long int int64;
typedef signed int int32;
typedef signed short int16;
typedef signed char int8;

namespace perception {

// Identifier for distinguishing types of messages.
typedef size_t MessageId;
// Used to identify processes.
typedef size_t ProcessId;

}

#define NULL 0