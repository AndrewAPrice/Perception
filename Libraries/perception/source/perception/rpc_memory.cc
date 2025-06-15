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

#include "perception/rpc_memory.h"

#include <atomic>
#include <map>
#include <mutex>
#include <set>

#include "perception/processes.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"

namespace perception {
namespace {

std::map<ProcessId, std::shared_ptr<SharedMemory>>
    shared_memory_for_sending_to_processes;

std::mutex mutex_for_shared_memory_for_sending_to_processes;

std::map<ProcessId, std::map<size_t, std::shared_ptr<SharedMemory>>>
    shared_memory_for_receiving_from_processes;

std::mutex mutex_for_shared_memory_for_receiving_from_processes;

std::set<ProcessId> processes_monitoring_for_death;

std::mutex mutex_for_processes_monitoring_for_death;

void OnProcessDied(ProcessId process_id) {
  {
    std::scoped_lock lock(mutex_for_processes_monitoring_for_death);
    processes_monitoring_for_death.erase(process_id);
  }

  {
    std::scoped_lock lock(mutex_for_shared_memory_for_sending_to_processes);
    shared_memory_for_sending_to_processes.erase(process_id);
  }

  {
    std::scoped_lock lock(mutex_for_shared_memory_for_receiving_from_processes);
    shared_memory_for_receiving_from_processes.erase(process_id);
  }
}

void MonitorForWhenProcessDies(ProcessId process_id) {
  {
    std::scoped_lock lock(mutex_for_processes_monitoring_for_death);
    if (processes_monitoring_for_death.contains(process_id)) return;
    processes_monitoring_for_death.insert(process_id);
  }
  NotifyUponProcessTermination(process_id,
                               [process_id]() { OnProcessDied(process_id); });
}

bool IsProcessStillAlive(ProcessId process_id) {
  std::scoped_lock lock(mutex_for_processes_monitoring_for_death);
  return processes_monitoring_for_death.contains(process_id);
}

}  // namespace

std::shared_ptr<SharedMemory> GetMemoryBufferForSendingToProcess(
    ProcessId process_id) {
  std::shared_ptr<SharedMemory> shared_memory;
  bool new_memory_created = false;
  {
    std::scoped_lock lock(mutex_for_shared_memory_for_sending_to_processes);
    auto itr = shared_memory_for_sending_to_processes.find(process_id);
    if (itr == shared_memory_for_sending_to_processes.end()) {
      // Create new shared memory block.
      shared_memory = SharedMemory::FromSize(1, 0);
      shared_memory_for_sending_to_processes[process_id] = shared_memory;
      // Clear start of shared memory.
      *(unsigned char*)**shared_memory = 0;
      new_memory_created = true;
    } else {
      shared_memory = itr->second;
    }
  }

  if (new_memory_created) {
    MonitorForWhenProcessDies(process_id);
  }

  while (true) {
    bool success = false;
    {
      std::scoped_lock lock(shared_memory->Mutex());
      void* shared_status_ptr = **shared_memory;
      std::atomic<unsigned char>* atomic_status =
          reinterpret_cast<std::atomic<unsigned char>*>(shared_status_ptr);

      unsigned char success = 0;
      unsigned char expected = 0;
      success = atomic_status->compare_exchange_weak(expected, 1);
    }

    if (success) break;

    // Also break if the process is no longer alive, otherwise this will forever
    // be looping.
    if (!IsProcessStillAlive(process_id)) break;

    Yield();
  }

  return shared_memory;
}

std::shared_ptr<SharedMemory> GetMemoryBufferForReceivingFromProcess(
    ProcessId process_id, size_t shared_memory_id) {
  std::scoped_lock lock(mutex_for_shared_memory_for_receiving_from_processes);
  auto itr = shared_memory_for_receiving_from_processes.find(process_id);
  if (itr == shared_memory_for_receiving_from_processes.end()) {
    MonitorForWhenProcessDies(process_id);

    itr = shared_memory_for_receiving_from_processes.emplace(
        process_id, std::map<size_t, std::shared_ptr<SharedMemory>>());
  }

  auto itr2 = itr->second.find(shared_memory_id);
  if (itr2 == itr->second.end()) {
    auto shared_memory = std::make_shared<SharedMemory>(shared_memory_id);
    itr->second[shared_memory_id] = shared_memory;
    return shared_memory;
  } else {
    return itr2->second;
  }
}

}  // namespace perception