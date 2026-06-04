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

#include "ide.h"
#include "perception/scheduler.h"
#include "perception/fibers.h"
#include "perception/processes.h"

using ::perception::IsDuplicateInstanceOfProcess;
using ::perception::HandOverControl;
using ::perception::Fiber;

int main(int argc, char *argv[]) {
  if (IsDuplicateInstanceOfProcess()) return 0;

  // Defer initialization to a fiber so that it runs in a fiber context
  // allowing it to yield while waiting for worker threads.
  Fiber::Create([]() {
    InitializeIdeControllers();
  })->WakeUp();

  HandOverControl();
  return 0;
}