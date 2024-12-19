// Copyright 2021 Google LLC
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

#include <functional>
#include <string>
#include <string_view>

#include "types.h"

namespace perception {

constexpr int kMaximumProcessNameLength = 88;

// Gets this ID of the currently running process.
ProcessId GetProcessId();

// Terminates the currently running process.
void TerminateProcess();

// Terminates a process.
void TerminateProcesss(ProcessId pid);

// Populates `pid` with the ID of the first process with the provided name.
// Returns true if succesful, or false if no process was found.
bool GetFirstProcessWithName(std::string_view name, ProcessId& pid);

// Retrieves the ID of each process with the provided name.
void ForEachProcessWithName(
    std::string_view name,
    const std::function<void(ProcessId)>& on_each_process);

// Loops through every running process.
void ForEachProcess(const std::function<void(ProcessId)>& on_each_process);

// Returns the name of the currently running process.
std::string GetProcessName();

// Returns the name of a process, or an empty string if the process doesn't
// exist.
std::string GetProcessName(ProcessId pid);

// Returns true if the process exists.
bool DoesProcessExist(ProcessId pid);

// Returns true if the process exists.
bool DoesProcessExist(std::string_view name);

// Registers that we want to be notified with a message upon the given process
// terminating. The handler is automatically unregistered upon terminating
// (it's safe to accidentally call StopNotifyingUponProcessTermination if the
// handler has triggered.)
MessageId NotifyUponProcessTermination(
    ProcessId pid, const std::function<void()>& on_termination);

// Registers that we don't want to be notified anymore about a process
// terminating.
void StopNotifyingUponProcessTermination(MessageId message_id);

// Creates a child process with a given name. Populates the `pid` with the new
// process's id. The new process doesn't begin executing until
// `StartExecutingChildProcess` is called. Only some processes are allowed to
// create child processes. Returns if the child process was successfully
// created. If the child process hasn't started executing yet, it will terminate
// if this process terminates.
bool CreateChildProcess(std::string_view name, size_t bitfield, ProcessId& pid);

// Unmaps a memory page from the current process and assigns it to a child
// process that hasn't began executing. The memory is unmapped from this process
// regardless of if this call succeeds. If the page already exists in the child
// process, nothing is set.
void SetChildProcessMemoryPage(ProcessId& child_pid, size_t source_address,
                               size_t destination_address);

// Creates a thread in the a child process. The child process will begin
// executing and will no longer terminate if the creator terminates.
void StartExecutingChildProcess(ProcessId& child_pid, size_t entry_address,
                                size_t params);

// Destroys a child process that hasn't began executing.
void DestroyChildProcess(ProcessId& child_pid);

// Returns whether there is another already running instance of this process.
bool IsDuplicateInstanceOfProcess();

}  // namespace perception
