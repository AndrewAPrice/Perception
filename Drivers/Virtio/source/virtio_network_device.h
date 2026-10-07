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
#include <string_view>

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

  // Programs the device's receive filter through the control virtqueue. A
  // no-op returning OK when the device lacks VIRTIO_NET_F_CTRL_RX.
  virtual Status SetMulticastFilter(
      const perception::devices::MulticastFilter& filter,
      perception::ProcessId sender) override;

 private:
  void HandleInterrupt();

  // Sends a command on the control virtqueue and waits for the device to
  // acknowledge it. Returns true if the device reported success. The caller
  // must hold `ctrl_mutex_`.
  bool SendControlCommand(uint8 command_class, uint8 command,
                          std::string_view data);

  // Sends a VIRTIO_NET_CTRL_RX on/off command. The caller must hold
  // `ctrl_mutex_`.
  bool SetRxMode(uint8 command, bool enabled);

  VirtioPciDevice virtio_pci_;
  uint8 mac_[6];
  perception::devices::NetworkListener::Client listener_;
  bool processing_interrupt_ = false;

  // RX Queue details
  QueueDetails rx_queue_;

  // TX Queue details
  std::mutex tx_mutex_;
  QueueDetails tx_queue_;

  // Whether VIRTIO_NET_F_CTRL_VQ and VIRTIO_NET_F_CTRL_RX were negotiated and
  // the control virtqueue is ready.
  bool has_ctrl_rx_ = false;

  // Serializes use of the control virtqueue.
  std::mutex ctrl_mutex_;

  // Control virtqueue details.
  QueueDetails ctrl_queue_;
};