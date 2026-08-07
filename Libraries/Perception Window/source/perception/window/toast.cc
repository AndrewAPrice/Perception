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

#include "perception/window/toast.h"

#include <string>

#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/services.h"
#include "perception/window/window_manager.h"

namespace perception {
namespace window {

void ShowToast(std::string_view title, std::string_view text) {
  auto window_manager =
      ::perception::FindFirstInstanceOfService<WindowManager>();
  if (window_manager) {
    ShowToastRequest request;
    request.title = std::string(title);
    request.text = std::string(text);
    window_manager->ShowToast(request, {});
  }
}

void ShowToast(std::string_view text) { ShowToast("", text); }

namespace {

void HandlePermissionDeniedToast(ProcessId process, Permission permission) {
  std::string process_name = GetProcessName(process);
  if (process_name.empty())
    process_name = "Process " + std::to_string(process);

  auto verbalization = GetPermissionVerbalization(permission);
  std::string text;
  if (verbalization) {
    text = "\"" + process_name + "\" was denied permission to " +
           std::string(*verbalization) + ".";
  } else {
    text = "\"" + process_name + "\" was denied permission.";
  }
  ShowToast("Permission Denied", text);
}

struct ToastHandlerRegistrar {
  ToastHandlerRegistrar() {
    SetPermissionDeniedToastHandler(HandlePermissionDeniedToast);
  }
};

static ToastHandlerRegistrar g_toast_handler_registrar;

}  // namespace

}  // namespace window
}  // namespace perception
