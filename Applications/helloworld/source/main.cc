// Copyright 2020 Google LLC
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
#include <vector>

#include "perception/fibers.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/time.h"
#include "perception/ui/builders/window.h"

using ::perception::Fiber;
using ::perception::GetProcessId;
using ::perception::MessageId;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::perception::ui::builders::IsDialog;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowTitle;
using ::perception::ui::builders::WindowBackgroundColor;

using ::perception::AfterDuration;
using ::perception::AfterTimeSinceKernelStarted;
using ::perception::GetTimeSinceKernelStarted;
using ::perception::SleepForDuration;
using ::perception::SleepUntilTimeSinceKernelStarted;

int main (int argc, char *argv[]) {
  auto a = Window(WindowTitle("Raspberry"), WindowBackgroundColor(0x0ed321ff));
  auto b = Window(WindowTitle("Blueberry"), WindowBackgroundColor(0xc5c20dff));
  auto c = Window(WindowTitle("Blackberry"), WindowBackgroundColor(0xa5214eff));
  auto d = Window(WindowTitle("Strawberry"), WindowBackgroundColor(0x90bdee));
  auto e = Window(WindowTitle("Boysenberry"), WindowBackgroundColor(0x25993fff));
  auto f = Window(WindowTitle("Popup Dialog"), WindowBackgroundColor(0x65e979ff), IsDialog());
  auto g = Window(WindowTitle("Another Dialog"), WindowBackgroundColor(0x7c169aff), IsDialog());

  std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count()
            << std::endl;
  std::cout << "Before" << std::endl;
  SleepForDuration(std::chrono::seconds(1));
  std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count()
            << std::endl;
  SleepForDuration(std::chrono::seconds(1));
  std::cout << "Kernel time: " << GetTimeSinceKernelStarted().count()
            << std::endl;

  AfterDuration(std::chrono::seconds(3), [&]() {
    std::cout << "3 (" << GetTimeSinceKernelStarted().count() << ")"
              << std::endl;
  });
  AfterDuration(std::chrono::seconds(1), [&]() {
    std::cout << "1 (" << GetTimeSinceKernelStarted().count() << ")"
              << std::endl;
  });
  AfterDuration(std::chrono::seconds(4), [&]() {
    std::cout << "4 (" << GetTimeSinceKernelStarted().count() << ")"
              << std::endl;
  });
  AfterDuration(std::chrono::seconds(2), [&]() {
    std::cout << "2 (" << GetTimeSinceKernelStarted().count() << ")"
              << std::endl;
  });

  AfterTimeSinceKernelStarted(
      GetTimeSinceKernelStarted() + std::chrono::milliseconds(2500), []() {
        std::cout << "2.5 (" << GetTimeSinceKernelStarted().count() << ")"
                  << std::endl;
      });

  perception::HandOverControl();
  return 0;
}