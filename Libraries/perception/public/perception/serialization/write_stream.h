// Copyright 2025 Google LLC
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

#include <types.h>

namespace perception {
namespace serialization {

class WriteStream {
 public:
  // Copies data into the stream.
  virtual void CopyDataIntoStream(const void *data, size_t size) = 0;

  // Copies data into the stream at a specific offset. This is isolated and does
  // not change 'CurrentOffset'.
  virtual void CopyDataIntoStream(const void *data, size_t size,
                                  size_t offset) = 0;

  // Skip forward the current offset.
  virtual void SkipForward(size_t size) = 0;

  // The current offset in the stream.
  virtual size_t CurrentOffset() = 0;
};

}  // namespace serialization
}  // namespace perception
