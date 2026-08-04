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

#include <mutex>

#include "driver.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/network_device.h"
#include "perception/pci.h"
#include "queue.h"
#include "types.h"
#include "virtio_pci_device.h"

class VirtioNetworkDevice : public perception::devices::NetworkDevice::Server,
                            public Driver {
 public:
  VirtioNetworkDevice(const perception::devices::PciDevice& device);

  virtual StatusOr<perception::devices::MacAddress> GetMacAddress() override;

  virtual Status SendPacket(
      const perception::devices::Packet& packet,
      perception::ProcessId sender) override;

  virtual Status SetPacketListener(
      const perception::devices::NetworkListener::Client& listener,
      perception::ProcessId sender) override;

 private:
  void HandleInterrupt();

  VirtioPciDevice virtio_pci_;
  uint8 mac_[6];
  perception::devices::NetworkListener::Client listener_;
  bool processing_interrupt_ = false;

  // RX Queue details
  QueueDetails rx_queue_;

  // TX Queue details
  std::mutex tx_mutex_;
  QueueDetails tx_queue_;
};