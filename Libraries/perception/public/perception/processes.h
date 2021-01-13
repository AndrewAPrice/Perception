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
#include <string_view>

namespace perception {

constexpr int kMaximumProcessNameLength = 88;

// Used to identify processes.
typedef size_t ProcessId;

// Gets this ID of the currently running process.
ProcessId GetProcessId();

// Terminates the currently running process.
void TerminateProcess();

// Populates `pid` with the ID of the first process with the provided name.
// Returns true if succesful, or false if no process was found.
bool GetFirstProcessWithName(std::string_view name, ProcessId& pid);

// Retrieves the ID of each process with the provided name.
void ForEachProcessWithName(std::string_view,
	const std::function<void(ProcessId)>& on_each_process);

}
