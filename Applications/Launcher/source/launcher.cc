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

#include "launcher.h"

#include <iostream>

#include "applications.h"
#include "launcher_window.h"

using LauncherService = ::permebuf::perception::Launcher;

/*
StatusOr<LauncherService::LaunchApplicationResponse>
Launcher::HandleLaunchApplication(
        ::perception::ProcessId sender,
        Permebuf<LauncherService::LaunchApplicationRequest> request) {
        std::cout << "TODO: Implement launch application." << std::endl;
        return ::perception::Status::UNIMPLEMENTED;
}*/

void Launcher::HandleShowLauncher(
    ::perception::ProcessId sender,
    const ::LauncherService::ShowLauncherMessage& message) {
  InitializeApplications();
  ShowLauncherWindow();
}