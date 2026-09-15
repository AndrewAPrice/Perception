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

#include "containers/aa_tree.h"
#include "scheduling/thread.h"
#include "types.h"

namespace processes {

struct Process;

// Manages the collection of threads owned by a process.
class ProcessThreads {
 public:
  // Constructs thread manager for the given process.
  ProcessThreads(Process& owner);

  // Destructor.
  ~ProcessThreads();

  // Range-based for loop support.
  auto begin() { return threads_.begin(); }
  auto end() { return threads_.end(); }

  // Returns the first thread in the process, or nullptr if empty.
  scheduling::Thread* FirstItem() { return threads_.FirstItem(); }

  // Returns the next thread in the process, or nullptr if end.
  scheduling::Thread* NextItem(scheduling::Thread* thread) {
    return threads_.NextItem(thread);
  }

  // Finds a thread by thread ID. Returns nullptr if not found.
  scheduling::Thread* Get(size_t tid) {
    return threads_.SearchForItemEqualToValue(tid);
  }

  // Searches for a thread by thread ID.
  scheduling::Thread* SearchForItemEqualToValue(size_t tid) {
    return Get(tid);
  }

  // Inserts a thread into the process.
  void Insert(scheduling::Thread* thread);

  // Removes a thread from the process.
  void Remove(scheduling::Thread* thread);

  // Returns the number of threads in the process.
  size_t count() const { return count_; }

  // Returns whether the process has no threads.
  bool IsEmpty() const { return count_ == 0; }

 private:
  Process& owner_;
  containers::AATree<scheduling::Thread, &scheduling::Thread::node_in_process,
                     &scheduling::Thread::id>
      threads_;
  size_t count_;
};

}  // namespace processes
