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

#include "driver_loader.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "perception/loader.h"
#include "perception/processes.h"
#include "perception/services.h"

using ::perception::DoesProcessExist;
using ::perception::GetService;
using ::perception::LoadApplicationRequest;
using ::perception::Loader;
using ::perception::ProcessId;

namespace {

struct DriverInfo {
  std::vector<std::string> arguments;
};

std::map<std::string, DriverInfo> drivers_to_load;

bool found_graphics_device = false;
bool found_keyboard_device = false;
bool found_pointing_device = false;
bool initial_drivers_loaded = false;

void LaunchDriverNow(const std::string& driver_name,
                     const std::vector<std::string>& arguments) {
  if (DoesProcessExist(driver_name)) return;

  std::cout << "Requesting to load " << driver_name;
  if (!arguments.empty()) {
    std::cout << " with args:";
    for (const auto& arg : arguments)
      std::cout << " " << arg;
  }
  std::cout << std::endl;

  LoadApplicationRequest request;
  request.name = driver_name;
  request.arguments = arguments;
  GetService<Loader>().LaunchApplication(request, nullptr);
}

}  // namespace

void AddDriverToLoad(std::string_view driver_name,
                     const std::vector<std::string>& arguments) {
  std::string name(driver_name);
  auto& dest_args = drivers_to_load[name].arguments;
  for (const auto& arg : arguments) {
    if (std::find(dest_args.begin(), dest_args.end(), arg) == dest_args.end())
      dest_args.push_back(arg);
  }
}

void RequestDriverLoad(std::string_view driver_name,
                       const std::vector<std::string>& arguments) {
  if (!initial_drivers_loaded) {
    AddDriverToLoad(driver_name, arguments);
    return;
  }
  LaunchDriverNow(std::string(driver_name), arguments);
}

void FoundGraphicsDevice() { found_graphics_device = true; }

bool HasFoundGraphicsDevice() { return found_graphics_device; }

void FoundKeyboardDevice() { found_keyboard_device = true; }

bool HasFoundKeyboardDevice() { return found_keyboard_device; }

void FoundPointingDevice() { found_pointing_device = true; }

bool HasFoundPointingDevice() { return found_pointing_device; }

void LoadAllRemainingDrivers() {
  initial_drivers_loaded = true;
  for (const auto& [driver_name, info] : drivers_to_load)
    LaunchDriverNow(driver_name, info.arguments);
  drivers_to_load.clear();
}