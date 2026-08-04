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

#include "virtio_tablet_device.h"

#include <algorithm>

#include "input_event.h"
#include "perception/devices/tablet_device.h"
#include "perception/devices/tablet_listener.h"
#include "status.h"
#include "types.h"

using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseCaptureState;
using ::perception::devices::PciDevice;
using ::perception::devices::TabletHoverEvent;
using ::perception::devices::TabletListener;

namespace {

constexpr uint16 kEvSyn = 0x00;
constexpr uint16 kEvKey = 0x01;
constexpr uint16 kEvAbs = 0x03;
constexpr uint16 kSynReport = 0x00;
constexpr uint16 kAbsX = 0x00;
constexpr uint16 kAbsY = 0x01;
constexpr float kVirtioAbsMax = 32767.0f;

}  // namespace

VirtioTabletDevice::VirtioTabletDevice(const PciDevice& device)
    : TabletDevice::Server({.defer_registration = true}), virtio_pci_(device) {
  virtio_pci_.Initialize();
  virtio_pci_.RegisterInterrupt([this]() { HandleInterrupt(); });

  StartServing();
  EnableDevice();
}

Status VirtioTabletDevice::SetTabletListener(
    const TabletListener::Client& listener) {
  tablet_listener_ = listener.IsValid()
                         ? std::make_unique<TabletListener::Client>(listener)
                         : nullptr;

  virtio_pci_.KickQueue(input_handler_.event_queue());
  return Status::OK;
}

Status VirtioTabletDevice::SetMouseCaptured(const MouseCaptureState& state) {
  is_captured_ = state.is_captured;
  if (is_captured_) {
    DisableDevice();
    if (mouse_device_) mouse_device_->EnableDevice();
  } else {
    if (mouse_device_) mouse_device_->DisableDevice();
    EnableDevice();
  }
  return Status::OK;
}

void VirtioTabletDevice::EnableDevice() { input_handler_.Enable(virtio_pci_); }

void VirtioTabletDevice::DisableDevice() {
  input_handler_.Disable(virtio_pci_);
}

void VirtioTabletDevice::HandleInterrupt() {
  input_handler_.HandleInterrupt(virtio_pci_, [this](const InputEvent& ev) {
    switch (ev.type) {
      case kEvAbs:
        if (ev.code == kAbsX) {
          current_x_ = std::max(0.0f, std::min(1.0f, ev.value / kVirtioAbsMax));
          position_changed_ = true;
        } else if (ev.code == kAbsY) {
          current_y_ = std::max(0.0f, std::min(1.0f, ev.value / kVirtioAbsMax));
          position_changed_ = true;
        }
        break;
      case kEvKey: {
        MouseButton button = VirtioInputHandler::MapButton(ev.code);
        if (button != MouseButton::Unknown && tablet_listener_) {
          MouseButtonEvent btn_event;
          btn_event.button = button;
          btn_event.is_pressed_down = (ev.value != 0);
          tablet_listener_->TabletButton(btn_event, nullptr);
        }
        break;
      }
      case kEvSyn:
        if (ev.code == kSynReport) {
          if (position_changed_) {
            if (tablet_listener_) {
              TabletHoverEvent pos_event;
              pos_event.x = current_x_;
              pos_event.y = current_y_;
              tablet_listener_->TabletHover(pos_event, nullptr);
            }
            position_changed_ = false;
          }
        }
        break;
    }
  });
}

void VirtioTabletDevice::SetMouseDevice(
    std::shared_ptr<VirtioMouseDevice> mouse_device) {
  mouse_device_ = mouse_device;
}
