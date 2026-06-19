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
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "perception/messages.h"
#include "perception/processes.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"

namespace perception {
namespace {

constexpr int kMaxConcurrentBuffersPerProcess = 8;

std::map<ProcessId, std::vector<std::shared_ptr<SharedMemory>>>
    shared_memory_for_sending_to_processes;

std::mutex mutex_for_shared_memory_for_sending_to_processes;

std::map<ProcessId, std::map<size_t, std::shared_ptr<SharedMemory>>>
    shared_memory_for_receiving_from_processes;

std::mutex mutex_for_shared_memory_for_receiving_from_processes;

std::set<ProcessId> processes_monitoring_for_death;

std::mutex mutex_for_processes_monitoring_for_death;

constexpr int kYieldAttemptsBeforeGettingNewMemory = 1000;

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

// Creates a memory buffer for sending data to a process. The caller should lock
// `mutex_for_shared_memory_for_sending_to_processes` before calling.
std::shared_ptr<SharedMemory> CreateNewMemoryBufferToSendToProcess(
    ProcessId process_id) {
  // Create new shared memory block.
  std::shared_ptr<SharedMemory> shared_memory =
      SharedMemory::FromSize(1, SharedMemory::kJoinersCanWrite);
  // Set first bit to be in use.
  *(unsigned char*)**shared_memory = 1;
  return shared_memory;
}

}  // namespace

std::shared_ptr<SharedMemory> GetMemoryBufferForSendingToProcess(
    ProcessId process_id) {
  std::scoped_lock lock(mutex_for_shared_memory_for_sending_to_processes);

  auto itr = shared_memory_for_sending_to_processes.find(process_id);
  if (itr == shared_memory_for_sending_to_processes.end()) {
    MonitorForWhenProcessDies(process_id);
    auto& vec = shared_memory_for_sending_to_processes[process_id];
    vec.resize(kMaxConcurrentBuffersPerProcess, nullptr);
    itr = shared_memory_for_sending_to_processes.find(process_id);
  }

  auto& vec = itr->second;
  std::shared_ptr<SharedMemory> selected_buffer = nullptr;

  for (size_t i = 0; i < kMaxConcurrentBuffersPerProcess; ++i) {
    auto& buf = vec[i];
    if (buf == nullptr) {
      selected_buffer = CreateNewMemoryBufferToSendToProcess(process_id);
      buf = selected_buffer;
      break;
    } else {
      std::scoped_lock lock(buf->Mutex());
      void* shared_status_ptr = **buf;
      std::atomic<unsigned char>* atomic_status =
          reinterpret_cast<std::atomic<unsigned char>*>(shared_status_ptr);
      if (atomic_status->load() == 0) {
        atomic_status->store(1);
        selected_buffer = buf;
        break;
      }
    }
  }

  if (selected_buffer == nullptr) {
    std::cout << "Error: Reached maximum concurrent RPC buffers ("
              << kMaxConcurrentBuffersPerProcess << ") for process "
              << process_id << std::endl;
    return nullptr;
  }

  return selected_buffer;
}

std::shared_ptr<SharedMemory>
GetMemoryBufferForSendingToProcessRegardlessOfIfInUse(ProcessId process_id,
                                                      size_t shared_memory_id) {
  std::scoped_lock lock(mutex_for_shared_memory_for_sending_to_processes);
  auto itr = shared_memory_for_sending_to_processes.find(process_id);
  if (itr == shared_memory_for_sending_to_processes.end()) return {};

  for (auto& buf : itr->second) {
    if (buf && buf->GetId() == shared_memory_id) {
      return buf;
    }
  }
  return {};
}

std::shared_ptr<SharedMemory> GetMemoryBufferForReceivingFromProcess(
    ProcessId process_id, size_t shared_memory_id) {
  std::scoped_lock lock(mutex_for_shared_memory_for_receiving_from_processes);
  auto itr = shared_memory_for_receiving_from_processes.find(process_id);
  if (itr == shared_memory_for_receiving_from_processes.end()) {
    MonitorForWhenProcessDies(process_id);

    itr = shared_memory_for_receiving_from_processes
              .emplace(process_id,
                       std::map<size_t, std::shared_ptr<SharedMemory>>())
              .first;
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

void SetMemoryBufferAsReadyForSendingNextMessageToProcess(
    SharedMemory& shared_memory) {
  (void)shared_memory.Join();
  if (shared_memory.CanWrite()) {
    std::scoped_lock lock(shared_memory.Mutex());
    *(unsigned char*)*shared_memory = 0;
  } else {
    std::cout << "Can't write to the shared memory sent to this process. The "
                 "shared memory can't be reused for future messages."
              << std::endl;
  }
}

}  // namespace perception