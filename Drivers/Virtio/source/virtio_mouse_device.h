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
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/devices/tablet_device.h"
#include "status.h"
#include "virtio_input_handler.h"
#include "virtio_pci_device.h"

class VirtioMouseDevice : public Driver,
                          public perception::devices::MouseDevice::Server {
 public:
  VirtioMouseDevice(const perception::devices::PciDevice& device);
  virtual ~VirtioMouseDevice() = default;

  Status SetMouseListener(
      const perception::devices::MouseListener::Client& listener) override;

  void HandleInterrupt();

  void EnableDevice();
  void DisableDevice();

 private:
  VirtioPciDevice virtio_pci_;
  VirtioInputHandler input_handler_;

  std::unique_ptr<perception::devices::MouseListener::Client> mouse_listener_;

  float accum_delta_x_ = 0.0f;
  float accum_delta_y_ = 0.0f;
  bool delta_changed_ = false;
  bool is_captured_ = false;
};
