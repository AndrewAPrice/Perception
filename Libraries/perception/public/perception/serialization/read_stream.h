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

#include <functional>

namespace perception {
namespace serialization {

class ReadStream {
 public:
  // Copies data out of the stream. Increments the pointer. Copies `0` if
  // the end of the stream has been reached.
  virtual void CopyDataOutOfStream(void* data, size_t size) = 0;

  // Returns whether the stream contains at least `bytes` of unread data.
  virtual bool ContainsAtLeast(size_t bytes = 1) = 0;

  // Skip forward the current offset.
  virtual void SkipForward(size_t size) = 0;

  // Calls the callback with a substream. While in the callback, it is undefined
  // behavior if the parent stream is read. The parent stream is progressed to
  // the end of substream regardless of if the callback function finished
  // reading it or not.
  virtual void ReadSubStream(
      size_t size,
      const std::function<void(ReadStream& sub_stream)>& on_sub_stream) = 0;
};

}  // namespace serialization
}  // namespace perception
