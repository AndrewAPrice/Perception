// Copyright 2023 Google LLC
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

class Process;

// Initializes profiling.
void InitializeProfiling();

// Starts profiling the system, passing the process that requested it.
void EnableProfiling(Process* process);

// Finishes profiling and outputs the data, passing the process that requested
// it.
void DisableAndOutputProfiling(Process* process);

// Notifies profiler that a process has exited.
void NotifyProfilerThatProcessExited(Process* process);
