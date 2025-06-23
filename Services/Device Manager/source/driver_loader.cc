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

#include <iostream>
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

std::vector<std::string> drivers_to_load;

}

void AddDriverToLoad(std::string_view driver_name) {
  drivers_to_load.push_back((std::string)driver_name);
}

void LoadAllRemainingDrivers() {
  for (const auto& driver_name : drivers_to_load) {
    if (DoesProcessExist(driver_name)) continue;

    LoadApplicationRequest request;
    request.name = driver_name;
    GetService<Loader>().LaunchApplication(request, {});
  }
  drivers_to_load.clear();
}