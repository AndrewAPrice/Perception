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

#include "processes/process.h"

#include "../../../Libraries/perception/public/perception/tracing.h"
#include "hardware/registers.h"
#include "ipc/messages.h"
#include "output/text_terminal.h"
#include "processes/process_manager.h"

namespace processes {

#ifdef ENABLE_TRACING

void EmitProcessCreatedTrace(Process* process) {
  if (!process) return;
  uint64 tsc = hardware::ReadTimestampCounter();
  uint32 pid = static_cast<uint32>(process->pid);
  uint8 len = static_cast<uint8>(strlen((char*)process->name));

  char packet[14];
  packet[0] = 0x07;  // PROCESS_CREATED opcode
  memcpy(&packet[1], (const char*)&tsc, 8);
  memcpy(&packet[9], (const char*)&pid, 4);
  packet[13] = static_cast<char>(len);

  output::ScopedPrintSource source(0, "Kernel", 2);
  for (size_t i = 0; i < 14; i++) output::print << packet[i];
  for (size_t i = 0; i < len; i++) output::print << process->name[i];
}

void EmitProcessTerminatedTrace(Process* process) {
  if (!process) return;
  uint64 tsc = hardware::ReadTimestampCounter();
  uint32 pid = static_cast<uint32>(process->pid);

  char packet[13];
  packet[0] = 0x08;  // PROCESS_TERMINATED opcode
  memcpy(&packet[1], (const char*)&tsc, 8);
  memcpy(&packet[9], (const char*)&pid, 4);

  output::ScopedPrintSource source(0, "Kernel", 2);
  for (size_t i = 0; i < 13; i++) output::print << packet[i];
}
#endif

void InitializeProcesses() {
  ProcessManager::Get().Initialize();
}

Process* CreateProcess(bool is_driver, bool can_create_processes,
                       bool can_set_focus, bool can_terminate_processes,
                       const char* name) {
  return ProcessManager::Get().CreateProcess(is_driver, can_create_processes,
                                             can_set_focus,
                                             can_terminate_processes, name);
}


void DestroyProcess(Process* process) {
  ProcessManager::Get().DestroyProcess(process);
}

bool AreAnyProcessesRunning() {
  return ProcessManager::Get().HasRunningProcesses();
}

void NotifyProcessOnDeath(Process* target, Process* notifyee, size_t event_id) {
  ProcessManager::Get().NotifyProcessOnDeath(target, notifyee, event_id);
}

void StopNotifyingProcessOnDeath(Process* notifyee, size_t event_id) {
  ProcessManager::Get().StopNotifyingProcessOnDeath(notifyee, event_id);
}

ProcessRef GetProcessFromPid(size_t pid) {
  return ProcessManager::Get().Find(pid);
}

ProcessRef GetProcessOrNextFromPid(size_t pid) {
  return ProcessManager::Get().FindNext(pid);
}

size_t QueryProcesses(const char* name, size_t min_pid, size_t* pids,
                      size_t max_results) {
  return ProcessManager::Get().QueryProcesses(name, min_pid, pids, max_results);
}

bool GetProcessName(size_t pid, char* name_out) {
  return ProcessManager::Get().GetProcessName(pid, name_out);
}

ProcessRef FindNextProcessWithName(const char* name, size_t min_pid) {
  return ProcessManager::Get().FindNextWithName(name, min_pid);
}

Process* CreateChildProcess(Process* parent, char* name, size_t bitfield) {
  return ProcessManager::Get().CreateChildProcess(parent, name, bitfield);
}

bool IsProcessAChildOfParent(Process* parent, Process* child) {
  return ProcessManager::Get().IsProcessAChildOfParent(parent, child);
}

void SetChildProcessMemoryPages(Process* parent, Process* child,
                                size_t source_address,
                                size_t destination_address, size_t page_count) {
  ProcessManager::Get().SetChildProcessMemoryPages(
      parent, child, source_address, destination_address, page_count);
}

void StartExecutingChildProcess(Process* parent, Process* child,
                                size_t entry_address, size_t params) {
  ProcessManager::Get().StartExecutingChildProcess(parent, child, entry_address,
                                                  params);
}

void DestroyChildProcess(Process* parent, Process* child) {
  ProcessManager::Get().DestroyChildProcess(parent, child);
}


void AwakeFutexInProcess(Process* process, size_t address) {
  if (!process->futex_wake_message_id) return;

  ipc::SendKernelMessageToProcess(process, process->futex_wake_message_id,
                                  /*param1=*/address, /*param2=*/0,
                                  /*param3=*/0, /*param4=*/0, /*param5=*/0);
}

}  // namespace processes
