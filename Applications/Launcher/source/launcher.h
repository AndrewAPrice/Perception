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
#include <string>

#include "permebuf/Libraries/perception/launcher.permebuf.h"

class Launcher : public ::permebuf::perception::Launcher::Server {
 public:
  /*	StatusOr<::permebuf::perception::Launcher::LaunchApplicationResponse>
                  HandleLaunchApplication(
                  ::perception::ProcessId sender,
                  Permebuf<::permebuf::perception::Launcher::LaunchApplicationRequest>
     request) override;
  */
  virtual void HandleShowLauncher(
      ::perception::ProcessId sender,
      const ::permebuf::perception::Launcher::ShowLauncherMessage& message)
      override;
};
