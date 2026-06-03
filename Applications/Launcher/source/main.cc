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

#include <iostream>
#include <sstream>
#include <vector>
#include <chrono>
#include <cstdio>

#include "launcher.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/processes.h"
#include "perception/time.h"
#include "perception/scheduler.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "perception/ui/layout.h"
#include "perception/ui/node.h"

using ::perception::HandOverControl;
using ::perception::ui::Layout;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

int main(int argc, char* argv[]) {
  auto window = UiWindow::DialogWithTitleBar(
      "Welcome!",
      Label::BasicLabel(
          "Welcome to Perception. Press the ESCAPE key to open the launcher.",
          [](Layout& layout) {
            layout.SetJustifyContent(YGJustifyCenter);
            layout.SetAlignContent(YGAlignCenter);
          },
          [](Label& label) {
            label.SetTextAlignment(TextAlignment::MiddleCenter);
          }));

  auto launcher = std::make_unique<Launcher>();

  // Enable CPU tracking so health metrics have CPU usage
  ::perception::SetThatProcessCaresAboutCpuTracking(true);

  // Fiber to print running processes status
  ::perception::Fiber::Create([]() {
    while (true) {
      ::perception::SleepForDuration(std::chrono::seconds(3));
      std::cout << "\n----------------------------------------------------------------------\n";
      std::cout << "  PID | Process Name   | CPU % | Shared Memory | Unique Memory | Services \n";
      std::cout << "------+----------------+-------+---------------+---------------+----------\n";

      ::perception::ForEachProcess([](::perception::ProcessId pid) {
        std::string name = ::perception::GetProcessName(pid);
        size_t unique_memory = 0;
        size_t shared_memory = 0;
        size_t creation_timestamp = 0;
        size_t registered_services = 0;
        uint8 cpu_percentages[8] = {0};
        ::perception::GetProcessHealthMetrics(pid, unique_memory, shared_memory,
                                              creation_timestamp, registered_services,
                                              cpu_percentages);

        int cpu_pct = ((int)cpu_percentages[0] * 100) / 255;
        
        printf(" %4d | %-14.14s |  %3d%% | %10ld KB | %10ld KB | %8ld \n",
               (int)pid,
               name.c_str(),
               cpu_pct,
               shared_memory / 1024,
               unique_memory / 1024,
               registered_services);
      });
      std::cout << "----------------------------------------------------------------------\n";
    }
  })->WakeUp();

  HandOverControl();
  return 0;
}
