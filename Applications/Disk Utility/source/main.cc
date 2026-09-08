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

#include <memory>

#include "disk_utility_window.h"
#include "perception/disk/disk_manager.h"
#include "perception/scheduler.h"

int main() {
  auto disk_manager = std::make_unique<perception::disk::DiskManager>();
  auto disk_utility_window =
      std::make_unique<DiskUtilityWindow>(*disk_manager);

  disk_utility_window->Initialize();
  disk_manager->Initialize();

  perception::HandOverControl();
  return 0;
}
