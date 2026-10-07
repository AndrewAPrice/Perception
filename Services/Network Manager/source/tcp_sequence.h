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

#pragma once

#include <chrono>

#include "types.h"

// A point in time measured from an arbitrary fixed epoch (in the service, the
// time since the kernel started, as returned by
// perception::GetTimeSinceKernelStarted()). Pure TCP modules take the current
// time as a parameter so they can be driven by a fake clock in tests.
using TcpTime = std::chrono::microseconds;

// Returns true if sequence number `a` comes before `b` (RFC 9293 modulo 2^32
// arithmetic).
constexpr bool SeqLess(uint32 a, uint32 b) {
  return static_cast<int32>(a - b) < 0;
}

// Returns true if sequence number `a` comes before or equals `b`.
constexpr bool SeqLessOrEqual(uint32 a, uint32 b) {
  return static_cast<int32>(a - b) <= 0;
}

// Returns true if sequence number `a` comes after `b`.
constexpr bool SeqGreater(uint32 a, uint32 b) { return SeqLess(b, a); }

// Returns true if sequence number `a` comes after or equals `b`.
constexpr bool SeqGreaterOrEqual(uint32 a, uint32 b) {
  return SeqLessOrEqual(b, a);
}
