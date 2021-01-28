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

#include "types.h"

#include <functional>
#include <string>
#include <string_view>

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
void ForEachProcessWithName(std::string_view name,
	const std::function<void(ProcessId)>& on_each_process);

// Returns the name of the currently running process.
std::string GetProcessName();

// Returns the name of a process, or an empty string if the process doesn't
// exist.
std::string GetProcessName(ProcessId pid); 

// Returns true if the process exists.
bool DoesProcessExist(ProcessId pid);

// Registers that we want to be notified with a message upon the given process
// terminating. The handler is automatically unregistered upon terminating
// (it's safe to accidentally call StopNotifyingUponProcessTermination if the
// handler has triggered.)
MessageId NotifyUponProcessTermination(ProcessId pid,
	const std::function<void()>& on_termination);

// Registers that we don't want to be notified anymore about a process
// terminating.
void StopNotifyingUponProcessTermination(MessageId message_id);

}
