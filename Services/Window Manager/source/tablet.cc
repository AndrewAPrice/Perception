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

#include "tablet.h"

#include <memory>
#include <vector>

#include "mouse.h"
#include "perception/devices/tablet_device.h"
#include "perception/devices/tablet_listener.h"
#include "perception/services.h"
#include "screen.h"

using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::devices::MouseCaptureState;
using ::perception::devices::TabletDevice;
using ::perception::devices::TabletHoverEvent;
using ::perception::devices::TabletListener;

namespace {

bool is_mouse_captured_state = false;
std::vector<TabletDevice::Client> active_tablets;

class WMTabletListener : public TabletListener::Server {
 public:
  Status TabletHover(const TabletHoverEvent& message) override {
    if (is_mouse_captured_state) return Status::OK;

    auto screen_size = GetScreenSize();
    ::perception::ui::Point target_position;
    target_position.x = message.x * screen_size.width;
    target_position.y = message.y * screen_size.height;

    SetMousePosition(target_position);
    return Status::OK;
  }

  Status TabletButton(
      const ::perception::devices::MouseButtonEvent& message) override {
    ProcessMouseButtonEvent(message);
    return Status::OK;
  }
};

std::unique_ptr<WMTabletListener> tablet_listener;

}  // namespace

void InitializeTablets() {
#ifndef TEST
  NotifyOnEachNewServiceInstance<TabletDevice>(
      [](TabletDevice::Client tablet_device) {
        if (!tablet_listener)
          tablet_listener = std::make_unique<WMTabletListener>();

        active_tablets.push_back(tablet_device);
        tablet_device.SetTabletListener(*tablet_listener);
        MouseCaptureState state;
        state.is_captured = is_mouse_captured_state;
        tablet_device.SetMouseCaptured(state, nullptr);
      });
#endif
}

void SetTabletsMouseCaptured(bool captured) {
  is_mouse_captured_state = captured;
  MouseCaptureState state;
  state.is_captured = captured;
  for (auto& tablet : active_tablets)
    if (tablet.IsValid()) tablet.SetMouseCaptured(state, nullptr);
}
