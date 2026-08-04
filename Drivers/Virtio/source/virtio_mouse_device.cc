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

#include "virtio_mouse_device.h"

#include <algorithm>
#include <iostream>

#include "input_event.h"
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/pci.h"
#include "status.h"
#include "types.h"

using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseListener;
using ::perception::devices::PciDevice;
using ::perception::devices::RelativeMousePositionEvent;

namespace {

constexpr uint16 kEvSyn = 0x00;
constexpr uint16 kEvKey = 0x01;
constexpr uint16 kEvRel = 0x02;
constexpr uint16 kSynReport = 0x00;
constexpr uint16 kRelX = 0x00;
constexpr uint16 kRelY = 0x01;

}  // namespace

VirtioMouseDevice::VirtioMouseDevice(const PciDevice& device)
    : MouseDevice::Server({.defer_registration = true}), virtio_pci_(device) {
  virtio_pci_.Initialize();
  virtio_pci_.RegisterInterrupt([this]() { HandleInterrupt(); });

  // Force initial hardware reset to leave device disabled on bootup
  DisableDevice();

  // Register services
  MouseDevice::Server::StartServing();
}

Status VirtioMouseDevice::SetMouseListener(
    const MouseListener::Client& listener) {
  mouse_listener_ = listener.IsValid()
                        ? std::make_unique<MouseListener::Client>(listener)
                        : nullptr;
  return Status::OK;
}

void VirtioMouseDevice::EnableDevice() { input_handler_.Enable(virtio_pci_); }

void VirtioMouseDevice::DisableDevice() { input_handler_.Disable(virtio_pci_); }

void VirtioMouseDevice::HandleInterrupt() {
  input_handler_.HandleInterrupt(virtio_pci_, [this](const InputEvent& ev) {
    switch (ev.type) {
      case kEvRel:
        switch (ev.code) {
          case kRelX:
            accum_delta_x_ += static_cast<float>(static_cast<int32>(ev.value));
            delta_changed_ = true;
            break;
          case kRelY:
            accum_delta_y_ += static_cast<float>(static_cast<int32>(ev.value));
            delta_changed_ = true;
            break;
        }
        break;
      case kEvKey: {
        MouseButton button = VirtioInputHandler::MapButton(ev.code);
        if (button != MouseButton::Unknown && mouse_listener_) {
          MouseButtonEvent btn_event;
          btn_event.button = button;
          btn_event.is_pressed_down = (ev.value != 0);
          mouse_listener_->MouseButton(btn_event, nullptr);
        }
        break;
      }
      case kEvSyn:
        if (ev.code == kSynReport) {
          if (delta_changed_) {
            if (mouse_listener_) {
              RelativeMousePositionEvent rel_event;
              rel_event.delta_x = accum_delta_x_;
              rel_event.delta_y = accum_delta_y_;
              mouse_listener_->MouseMove(rel_event, nullptr);
            }
            accum_delta_x_ = 0.0f;
            accum_delta_y_ = 0.0f;
            delta_changed_ = false;
          }
        }
        break;
    }
  });
}
