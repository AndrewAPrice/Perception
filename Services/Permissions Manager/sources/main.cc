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

#include <algorithm>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "default_permissions.h"
#include "perception/fibers.h"
#include "perception/permissions.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/ui/components/block.h"
#include "perception/ui/components/button.h"
#include "perception/ui/components/checkbox.h"
#include "perception/ui/components/container.h"
#include "perception/ui/components/label.h"
#include "perception/ui/components/ui_window.h"
#include "permission_verbalization.h"
#include "permissions_cache.h"

using ::StatusOr;
using ::perception::Fiber;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::GetProcessName;
using ::perception::HandOverControl;
using ::perception::NotifyUponProcessTermination;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::perception::ui::Layout;
using ::perception::ui::Node;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Block;
using ::perception::ui::components::Button;
using ::perception::ui::components::Checkbox;
using ::perception::ui::components::Container;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

namespace {

// Mutex to protect the active dialog list.
std::mutex g_dialogs_mutex;

// Active dialog window nodes to keep them alive.
std::vector<std::shared_ptr<Node>> g_active_dialogs;

}  // namespace

class PermissionsManager : public ::perception::PermissionsManager::Server {
 public:
  PermissionsManager() {}
  virtual ~PermissionsManager() {}

  virtual StatusOr<::perception::DoesProcessHavePermissionResponse>
  DoesProcessHavePermission(
      const ::perception::DoesProcessHavePermissionRequest& request,
      ProcessId sender) override {
    ProcessId target_pid = request.process;
    Permission permission = request.permission;

    // Check the cached pid permissions first.
    if (auto has_permission = GetCachedPidPermission(target_pid, permission)) {
      ::perception::DoesProcessHavePermissionResponse response;
      response.has_permission = *has_permission;
      return response;
    }

    // Get the target process name.
    std::string process_name = GetProcessName(target_pid);
    if (process_name.empty()) {
      // If process doesn't exist or terminated, deny permission.
      ::perception::DoesProcessHavePermissionResponse response;
      response.has_permission = false;
      return response;
    }

    // Check the cached process name permissions.
    if (auto has_permission =
            GetCachedProcessNamePermission(process_name, permission)) {
      ::perception::DoesProcessHavePermissionResponse response;
      response.has_permission = *has_permission;
      return response;
    }

    // Check if the permission has a valid verbalization. If it doesn't,
    // this is not a valid permission that the Permissions Manager is aware of,
    // so just return false.
    auto verbalization =
        GetPermissionRequestVerbalization(process_name, permission);
    if (!verbalization.has_value()) {
      ::perception::DoesProcessHavePermissionResponse response;
      response.has_permission = false;
      return response;
    }

    // Show a dialog saying:
    // "<Process name> is requesting permission to <verbalization of the
    // permission>."
    Fiber* current_fiber = GetCurrentlyExecutingFiber();

    // Thread-safe or block-scope variables for recording user choice.
    struct DialogState {
      bool has_result = false;
      bool approved = false;
      bool remember = false;
      std::weak_ptr<Node> dialog_node;
    };
    auto state = std::make_shared<DialogState>();

    auto window_node = UiWindow::DialogWithTitleBar(
        "Permission Request",
        [current_fiber, state](UiWindow& window) {
          window.OnClose([current_fiber, state]() {
            if (!state->has_result) {
              state->approved = false;
              state->remember = false;
              state->has_result = true;
              if (current_fiber) {
                current_fiber->WakeUp();
              }
            }
            auto strong_node = state->dialog_node.lock();
            if (strong_node) {
              ::perception::Defer([strong_node]() {
                std::scoped_lock lock(g_dialogs_mutex);
                auto it = std::find(g_active_dialogs.begin(),
                                    g_active_dialogs.end(), strong_node);
                if (it != g_active_dialogs.end()) {
                  g_active_dialogs.erase(it);
                }
              });
            }
          });
        },
        [](Layout& layout) { layout.SetWidth(360.0f); },
        Container::VerticalContainer(
            [](Layout& layout) { layout.SetAlignItems(YGAlignStretch); },
            Label::BasicLabel(*verbalization),
            Checkbox::BasicCheckbox(
                "Remember for all instances of this process.", false,
                [state](bool checked) { state->remember = checked; }),
            Container::HorizontalContainer(
                [](Layout& layout) {
                  layout.SetJustifyContent(YGJustifyFlexEnd);
                },
                Button::TextButton(
                    "Deny",
                    [current_fiber, state]() {
                      state->approved = false;
                      state->has_result = true;
                      if (current_fiber) {
                        current_fiber->WakeUp();
                      }
                      auto strong_node = state->dialog_node.lock();
                      if (strong_node) {
                        auto window = strong_node->Get<UiWindow>();
                        if (window) window->Close();
                      }
                    },
                    [](Button& button) {
                      button.SetButtonStyle(Button::ButtonStyle::LIGHT);
                    }),
                Button::TextButton(
                    "Allow",
                    [current_fiber, state]() {
                      state->approved = true;
                      state->has_result = true;
                      if (current_fiber) {
                        current_fiber->WakeUp();
                      }
                      auto strong_node = state->dialog_node.lock();
                      if (strong_node) {
                        auto window = strong_node->Get<UiWindow>();
                        if (window) window->Close();
                      }
                    },
                    [](Button& button) {
                      button.SetButtonStyle(Button::ButtonStyle::PRIMARY);
                    }))),
        &state->dialog_node);

    {
      std::scoped_lock lock(g_dialogs_mutex);
      g_active_dialogs.push_back(window_node);
    }

    // Suspend the current fiber until the user clicks one of the options or
    // closes the window.
    Sleep();

    // Save the permission choice based on remember checkbox setting.
    if (state->remember) {
      SetCachedProcessNamePermission(process_name, permission, state->approved);
    } else {
      SetCachedPidPermission(target_pid, permission, state->approved);
    }

    ::perception::DoesProcessHavePermissionResponse response;
    response.has_permission = state->approved;
    return response;
  }
};

int main(int argc, char* argv[]) {
  // Load default permissions for core services.
  PopulateInitialPermissions();

  // Instantiate the server. The base Server class automatically registers it.
  auto permissions_manager = std::make_unique<PermissionsManager>();

  // Initialize registry permissions asynchronously once Registry is ready.
  InitializeRegistryPermissions();

  // Hand over control to the perception scheduler/event loop.
  HandOverControl();

  return 0;
}
