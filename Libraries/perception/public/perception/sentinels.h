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

// Values that syscalls return in place of an address to report that they
// failed. These are part of the syscall ABI, so this header is shared by the
// kernel and by userland rather than each hardcoding its own copy.
//
// The values sit at the very top of the 64 bit range because every alternative
// is a legitimate result: 0 is a real physical page, and 1 is a real (if
// unaligned) address. Being unaligned and outside any canonical address range
// means neither value can ever be produced by a successful call.
//
// Deliberately dependency free, so that the kernel can include it from its own
// `types.h` before anything else exists.
namespace perception {

// An operation failed for a reason other than running out of memory.
constexpr unsigned long long kErrorSentinel = ~0ULL;

// An allocation failed because memory was exhausted.
constexpr unsigned long long kOutOfMemorySentinel = ~0ULL - 1;

}  // namespace perception
