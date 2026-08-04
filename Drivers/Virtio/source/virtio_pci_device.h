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

#include <functional>

#include "perception/devices/device_manager.h"
#include "perception/pci.h"
#include "queue.h"
#include "types.h"

class VirtioPciDevice {
 public:
  explicit VirtioPciDevice(const perception::devices::PciDevice& device);
  ~VirtioPciDevice() = default;

  bool Initialize();

  const perception::devices::PciDevice& device() const { return device_; }
  uint16 io_base() const { return io_base_; }
  volatile uint8* common_cfg() const { return common_cfg_; }
  volatile uint8* notify_cfg() const { return notify_cfg_; }
  volatile uint8* isr_cfg() const { return isr_cfg_; }
  volatile uint8* device_cfg() const { return device_cfg_; }
  size_t isr_phys() const { return isr_phys_; }
  uint32 notify_off_multiplier() const { return notify_off_multiplier_; }
  uint8 interrupt_line() const { return interrupt_line_; }

  bool is_modern() const { return common_cfg_ != nullptr; }

  void Reset();
  void NegotiateFeatures(uint32 disable_features_mask = (1U << 29));
  void SetDriverOk();

  void KickQueue(const QueueDetails& queue);
  void RegisterInterrupt(std::function<void()> handler, uint8 read_mask = 1);

 private:
  perception::devices::PciDevice device_;
  uint16 io_base_ = 0;
  volatile uint8* common_cfg_ = nullptr;
  volatile uint8* notify_cfg_ = nullptr;
  volatile uint8* isr_cfg_ = nullptr;
  volatile uint8* device_cfg_ = nullptr;
  size_t isr_phys_ = 0;
  uint32 notify_off_multiplier_ = 0;
  uint8 interrupt_line_ = 0;
};
