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
#include <memory>
#include <string>
#include <vector>

#include "perception/ui/image.h"

// Represents an application that can be launched.
struct Application {
  // The name of the application.
  std::string name;

  // The path to the application.
  std::string path;

  // The description of the appliction.
  std::string description;

  // The icon of the application.
  std::shared_ptr<::perception::ui::Image> icon;
};

// Scans for applications.
void ScanForApplications();

// Returns the list of applications.
const std::vector<Application>& GetApplications();

// Registers a callback to be notified when a new application is found.
void RegisterApplicationFoundCallback(
    std::function<void(const Application&)> callback);

// Registers a callback to be notified when the scanning is finished.
void RegisterScanFinishedCallback(std::function<void()> callback);
