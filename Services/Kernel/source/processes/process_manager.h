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
#include "containers/spinlock.h"
#include "processes/process.h"
#include "types.h"

namespace processes {

// Singleton managing global process lifecycle, PID allocation, and process lookup.
class ProcessManager {
 public:
  // Returns the singleton instance of ProcessManager.
  static ProcessManager& Get();

  // Initializes process tracking structures.
  void Initialize();

  // Creates a new process. Returns nullptr on error.
  Process* CreateProcess(bool is_driver, bool can_create_processes,
                         bool can_set_focus = false,
                         bool can_terminate_processes = false,
                         const char* name = nullptr);


  // Destroys a process and releases all of its resources.
  void DestroyProcess(Process* process);

  // Returns a held reference to the process with the given PID, or an empty
  // reference if it is not found or is being torn down.
  ProcessRef Find(size_t pid);

  // Returns a held reference to the process with the given PID, or the next
  // highest PID >= pid.
  ProcessRef FindNext(size_t pid);

  // Queries processes matching name starting from min_pid,
  // filling pids up to max_results under a single lock acquisition.
  // Returns total number of matching processes found.
  size_t QueryProcesses(const char* name, size_t min_pid, size_t* pids,
                        size_t max_results);

  // Safely copies the name of a process into name_out under the manager lock.
  // Returns true if the process was found.
  bool GetProcessName(size_t pid, char* name_out);

  // Returns a held reference to the next process at or above min_pid with the
  // given name.
  ProcessRef FindNextWithName(const char* name, size_t min_pid);

  // Returns whether any processes are running.
  bool HasRunningProcesses();

  // Registers that notifyee wants to be notified when target dies.
  void NotifyProcessOnDeath(Process* target, Process* notifyee, size_t event_id);

  // Unregisters death notification for notifyee with the given event ID.
  void StopNotifyingProcessOnDeath(Process* notifyee, size_t event_id);

  // Creates a child process in the creating state.
  Process* CreateChildProcess(Process* parent, char* name, size_t bitfield);

  // Transfers memory pages from parent to child.
  void SetChildProcessMemoryPages(Process* parent, Process* child,
                                  size_t source_address,
                                  size_t destination_address, size_t page_count);

  // Starts execution of a child process.
  void StartExecutingChildProcess(Process* parent, Process* child,
                                  size_t entry_address, size_t params);

  // Destroys a child process in the creating state.
  void DestroyChildProcess(Process* parent, Process* child);

  // Returns whether child is a child of parent.
  bool IsProcessAChildOfParent(Process* parent, Process* child);
  // Constructs the ProcessManager.
  ProcessManager();

 private:

  // Maximum process ID supported by the lockless O(1) PID lookup table.
  static constexpr size_t kMaxFastProcesses = 1024;

  containers::InterruptSafeSpinlock lock_;
  size_t last_assigned_pid_;
  Process* processes_by_pid_[kMaxFastProcesses];
  containers::AATree<Process, &Process::node_in_all_processes, &Process::pid>
      all_processes_;
};

}  // namespace processes
