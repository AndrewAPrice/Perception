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

#include <iostream>
#include <memory>
#include <vector>

#include "intel_hda.h"
#include "perception/devices/device_manager.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/threads.h"

using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::IsDuplicateInstanceOfProcess;
using ::perception::SetThreadPriority;
using ::perception::ThreadPriority;
using ::perception::devices::DeviceManager;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;

namespace {

std::vector<std::unique_ptr<IntelHdaController>> controllers;

void InitializeIntelHdaControllers() {
  PciDeviceFilters filters;

  PciDeviceFilter base_class_filter;
  base_class_filter.key = PciDeviceFilter::Key::BASE_CLASS;
  base_class_filter.value = 0x04;  // Multimedia
  filters.filters.push_back(base_class_filter);

  PciDeviceFilter sub_class_filter;
  sub_class_filter.key = PciDeviceFilter::Key::SUB_CLASS;
  sub_class_filter.value = 0x03;  // Audio Controller
  filters.filters.push_back(sub_class_filter);

  auto status_or_devices = GetService<DeviceManager>().QueryPciDevices(filters);
  if (!status_or_devices) return;

  for (const auto& device : status_or_devices->devices) {
    auto controller = std::make_unique<IntelHdaController>(
        device.bus, device.slot, device.function);
    if (controller->Initialize()) controllers.push_back(std::move(controller));
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (IsDuplicateInstanceOfProcess()) return 0;

  SetThreadPriority(ThreadPriority::InterruptDriver);
  InitializeIntelHdaControllers();

  if (controllers.empty()) {
    std::cout << "No Intel HD Audio controllers found. Exiting." << std::endl;
    return 0;
  }

  HandOverControl();
  return 0;
}
