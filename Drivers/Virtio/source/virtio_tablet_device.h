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

#pragma once

#include <memory>

#include "driver.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/tablet_device.h"
#include "perception/devices/tablet_listener.h"
#include "status.h"
#include "virtio_input_handler.h"
#include "virtio_mouse_device.h"
#include "virtio_pci_device.h"

class VirtioTabletDevice : public Driver,
                           public perception::devices::TabletDevice::Server {
 public:
  VirtioTabletDevice(const perception::devices::PciDevice& device);
  virtual ~VirtioTabletDevice() = default;

  Status SetTabletListener(
      const perception::devices::TabletListener::Client& listener) override;
  Status SetMouseCaptured(
      const perception::devices::MouseCaptureState& state) override;

  void HandleInterrupt();
  void SetMouseDevice(std::shared_ptr<VirtioMouseDevice> mouse_device);

 private:
  void EnableDevice();
  void DisableDevice();

  VirtioPciDevice virtio_pci_;
  VirtioInputHandler input_handler_;

  std::unique_ptr<perception::devices::TabletListener::Client> tablet_listener_;
  std::shared_ptr<VirtioMouseDevice> mouse_device_;

  float current_x_ = 0.0f;
  float current_y_ = 0.0f;
  bool position_changed_ = false;
  float accum_scroll_x_ = 0.0f;
  float accum_scroll_y_ = 0.0f;
  bool scroll_changed_ = false;
  bool is_captured_ = false;
};
