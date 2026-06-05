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

#include "mount_listener.h"

#include <iostream>

#include "settings_loader.h"

::perception::Status MountListener::FileSystemMounted(
    const ::perception::FileSystemMountEvent& event,
    ::perception::ProcessId sender) {
  ScanAndLoadSettings("/" + event.mount_point);
  return ::perception::Status::OK;
}
