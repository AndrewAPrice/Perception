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
#include <vector>

#include "driver.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/tablet_device.h"
#include "perception/devices/tablet_listener.h"
#include "queue.h"
#include "status.h"

struct VirtioInputEvent {
  uint16 type;
  uint16 code;
  uint32 value;
} __attribute__((packed));

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

 private:
  perception::devices::PciDevice device_;
  uint16 io_base_ = 0;
  volatile uint8* common_cfg_ = nullptr;
  volatile uint8* notify_cfg_ = nullptr;
  volatile uint8* isr_cfg_ = nullptr;
  size_t isr_phys_ = 0;
  volatile uint8* device_cfg_ = nullptr;
  uint32 notify_off_multiplier_ = 0;

  QueueDetails event_queue_;
  QueueDetails status_queue_;
  std::unique_ptr<perception::devices::TabletListener::Client> tablet_listener_;

  float current_x_ = 0.0f;
  float current_y_ = 0.0f;
  bool position_changed_ = false;
  bool is_captured_ = false;
};
