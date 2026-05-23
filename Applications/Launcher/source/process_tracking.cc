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

#include "process_tracking.h"

#include <chrono>

#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "process_details_window.h"
#include "processes_tab.h"

namespace {

int cpu_tracking_ref_count = 0;
bool processes_fiber_running = false;

bool IsAnyCpuTrackingActive() {
  return IsProcessesTabVisible() || AreAnyProcessDetailsWindowsOpen();
}

}  // namespace

void IncrementCpuTracking() {
  cpu_tracking_ref_count++;
  if (cpu_tracking_ref_count == 1) {
    perception::SetThatProcessCaresAboutCpuTracking(true);
  }
}

void DecrementCpuTracking() {
  cpu_tracking_ref_count--;
  if (cpu_tracking_ref_count == 0) {
    perception::SetThatProcessCaresAboutCpuTracking(false);
  }
}

void StartQueryFiber() {
  if (processes_fiber_running) return;
  processes_fiber_running = true;

  perception::Fiber::Create([]() {
    while (IsAnyCpuTrackingActive()) {
      perception::SleepForDuration(std::chrono::seconds(1));
      if (!IsAnyCpuTrackingActive()) break;
      ::perception::Defer([]() {
        if (IsProcessesTabVisible()) {
          RebuildRunningProcesses();
        }
        UpdateOpenProcessDetailsWindows();
      });
    }
    processes_fiber_running = false;
  })->WakeUp();
}
