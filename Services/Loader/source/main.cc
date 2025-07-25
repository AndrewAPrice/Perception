// Copyright 2024 Google LLC
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

#include "loader_server.h"
#include "multiboot.h"
#include "perception/scheduler.h"

using ::perception::HandOverControl;

int main(int argc, char* argv[]) {
  // Load the multiboot modules first.
  LoadMultibootModules();

  // Create the loader server that listens to requests to launch executables.
  auto loader_server = std::make_unique<LoaderServer>();

  HandOverControl();
  return 0;
}