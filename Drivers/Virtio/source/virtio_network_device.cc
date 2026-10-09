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

#include "virtio_network_device.h"

#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "perception/cache.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/network_device.h"
#include "perception/interrupts.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/permissions.h"
#include "perception/port_io.h"
#include "perception/time.h"
#include "queue.h"
#include "types.h"

using ::perception::DoesProcessHavePermission;
using ::perception::FlushRange;
using ::perception::kPageSize;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPort;
using ::perception::SleepForDuration;
using ::perception::devices::MacAddress;
using ::perception::devices::MulticastFilter;
using ::perception::devices::NetworkListener;
using ::perception::devices::Packet;
using ::perception::devices::PciDevice;

namespace {

// Number of bytes in an Ethernet MAC address.
constexpr size_t kMacAddressLength = 6;
// Offset of the 6-byte MAC address in legacy virtio-net device configuration.
constexpr uint16 kVirtioNetConfigMacOffset = 20;
// Offset of the 16-bit status field in legacy virtio-net device configuration.
constexpr uint16 kVirtioNetConfigStatusOffset = 26;
// Receive virtqueue index.
constexpr uint16 kRxQueueIndex = 0;
// Transmit virtqueue index.
constexpr uint16 kTxQueueIndex = 1;
// Control virtqueue index when VIRTIO_NET_F_MQ is not negotiated.
constexpr uint16 kCtrlQueueIndex = 2;
// Size in bytes of each pre-allocated receive buffer page.
constexpr uint32 kRxBufferSize = 4096;
// Descriptor flag: the buffer continues in the descriptor named by `next`.
constexpr uint16 kVringDescFNext = 1;
// Descriptor flag: the buffer is device-writable.
constexpr uint16 kVringDescFWrite = 2;
// Byte size of the legacy virtio_net_hdr prepended to each packet.
constexpr size_t kVirtioNetHeaderSize = 10;
// Maximum packet payload size accepted by SendPacket.
constexpr size_t kMaxPacketDataSize = 4000;
// Byte size of the 3-page queue ring allocation for 256-entry RX/TX queues.
constexpr size_t kQueueMemoryFlushSize = 12288;
// Byte size of the available ring header (flags + idx + used_event).
constexpr size_t kAvailRingHeaderSize = 6;
// Byte size of each element in the available ring.
constexpr size_t kAvailRingElementSize = 2;
// Bitmask for reading the interrupt status register.
constexpr uint8 kIsrReadMask = 1;
// Legacy VirtIO PCI interrupt status register offset.
constexpr uint16 kVirtioPciIsr = 19;

// Feature bit: the device has a control virtqueue.
constexpr uint32 kVirtioNetFCtrlVq = 1U << 17;
// Feature bit: the control virtqueue accepts RX mode and MAC filter commands.
constexpr uint32 kVirtioNetFCtrlRx = 1U << 18;
// Features required to program the receive filter.
constexpr uint32 kReceiveFilterFeatures = kVirtioNetFCtrlVq | kVirtioNetFCtrlRx;

// Control command class for RX mode switches.
constexpr uint8 kVirtioNetCtrlRx = 0;
// RX mode command: deliver every frame.
constexpr uint8 kVirtioNetCtrlRxPromisc = 0;
// RX mode command: deliver every multicast frame.
constexpr uint8 kVirtioNetCtrlRxAllMulti = 1;
// Control command class for MAC filtering.
constexpr uint8 kVirtioNetCtrlMac = 1;
// MAC command: replace the unicast and multicast filter tables.
constexpr uint8 kVirtioNetCtrlMacTableSet = 0;
// Ack value written by the device on success.
constexpr uint8 kVirtioNetOk = 0;
// Ack value written by the device on failure.
constexpr uint8 kVirtioNetErr = 1;

// Byte size of the class + command header of a control command.
constexpr size_t kCtrlHeaderSize = 2;
// Byte size of the device-written ack.
constexpr uint32 kCtrlAckSize = 1;
// Offset of the ack within the control buffer page. The command header and
// data occupy the bytes before it.
constexpr size_t kCtrlAckOffset = kPageSize - kCtrlAckSize;
// Descriptor holding the command header and data.
constexpr uint16 kCtrlOutDescIndex = 0;
// Descriptor holding the ack.
constexpr uint16 kCtrlAckDescIndex = 1;
// Delay between checks for a control command's completion.
constexpr auto kCtrlPollInterval = std::chrono::milliseconds(1);
// Maximum number of completion checks before a control command times out.
constexpr int kCtrlMaxPolls = 1000;
// Combined MAC filter table capacity of QEMU's virtio-net (MAC_TABLE_ENTRIES).
// Larger tables make QEMU fall back to accepting all frames of that type.
constexpr size_t kMaxMacTableEntries = 64;
// Number of unicast entries programmed (the device's own MAC).
constexpr size_t kUnicastTableEntries = 1;
// Most multicast MACs programmed before falling back to all-multicast.
constexpr size_t kMaxMulticastTableEntries =
    kMaxMacTableEntries - kUnicastTableEntries;

// Appends a virtio_net_ctrl_mac table (le32 entry count followed by the MACs).
void AppendMacTable(std::string& table, const std::vector<const uint8*>& macs) {
  uint32 entries = macs.size();
  table.append(reinterpret_cast<const char*>(&entries), sizeof(entries));
  for (const uint8* mac : macs)
    table.append(reinterpret_cast<const char*>(mac), kMacAddressLength);
}

}  // namespace

VirtioNetworkDevice::VirtioNetworkDevice(const PciDevice& device)
    : NetworkDevice::Server({.defer_registration = true}), virtio_pci_(device) {
  virtio_pci_.Initialize(/*force_legacy=*/true);

  uint16 io_base = virtio_pci_.io_base();

  // Read Hardware MAC address from Virtio Configuration space (offset 20).
  for (int i = 0; i < kMacAddressLength; i++) {
    mac_[i] = Read8BitsFromPort(io_base + kVirtioNetConfigMacOffset + i);
  }

  // Perform Legacy Virtio Reset & Acknowledge handshake. The receive filter
  // features only change the control virtqueue, not the RX/TX header layout.
  // A partial offer is unusable, so negotiate nothing in that case.
  uint32 features = virtio_pci_.Reset(kReceiveFilterFeatures);
  if (features != kReceiveFilterFeatures && features != 0) {
    virtio_pci_.Reset();
    features = 0;
  }

  // Initialize Virtqueues (0 for RX, 1 for TX)
  rx_queue_.Setup(kRxQueueIndex, io_base);
  tx_queue_.Setup(kTxQueueIndex, io_base);
  if (!rx_queue_.desc || !rx_queue_.avail || !tx_queue_.desc ||
      !tx_queue_.avail) {
    std::cout << "VirtioNetworkDevice: Virtqueue setup failed!" << std::endl;
    return;
  }

  if (features == kReceiveFilterFeatures) {
    ctrl_queue_.Setup(kCtrlQueueIndex, io_base);
    has_ctrl_rx_ = ctrl_queue_.desc && ctrl_queue_.avail &&
                   ctrl_queue_.size > kCtrlAckDescIndex &&
                   ctrl_queue_.buffers_virt[0] != nullptr;
  }

  // Fill the receive descriptors with pre-allocated memory pages
  for (int i = 0; i < rx_queue_.size; i++) {
    rx_queue_.desc[i].addr = rx_queue_.buffers_phys[i];
    rx_queue_.desc[i].len = kRxBufferSize;
    rx_queue_.desc[i].flags = kVringDescFWrite;  // Writable by device
    rx_queue_.desc[i].next = 0;

    rx_queue_.avail->ring[i] = i;
  }
  rx_queue_.avail->flags = 0;
  __asm__ __volatile__("" ::: "memory");
  rx_queue_.avail->idx = rx_queue_.size;
  __asm__ __volatile__("" ::: "memory");

  FlushRange(rx_queue_.mem, kQueueMemoryFlushSize);

  // Initial RX queue notification
  virtio_pci_.KickQueue(rx_queue_);

  // Set DRIVER_OK status bit
  virtio_pci_.SetDriverOk();

  // Read the link status.
  (void)Read16BitsFromPort(io_base + kVirtioNetConfigStatusOffset);

  // Register Hardware Interrupt Handler using loop over port read to clear
  // ISR and prevent interrupt storm.
  virtio_pci_.RegisterInterrupt([this]() { HandleInterrupt(); }, kIsrReadMask);

  StartServing();
}

StatusOr<MacAddress> VirtioNetworkDevice::GetMacAddress() {
  MacAddress response;
  for (int i = 0; i < kMacAddressLength; i++) response.mac[i] = mac_[i];
  return response;
}

Status VirtioNetworkDevice::SendPacket(const Packet& packet, ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanUseNetworkDevice))
    return Status::NOT_ALLOWED;

  std::lock_guard<std::mutex> lock(tx_mutex_);
  // Reclaim completed transmit descriptors from device.
  tx_queue_.last_seen_used = tx_queue_.used->idx;

  // Check if transmit queue is full.
  uint16 tx_outstanding = tx_queue_.avail->idx - tx_queue_.last_seen_used;
  if (tx_outstanding >= tx_queue_.size) {
    std::cout << "Transmit Queue is full!" << std::endl;
    return Status::OUT_OF_MEMORY;
  }

  // Get the next descriptor slot index.
  uint16 desc_idx = tx_queue_.avail->idx % tx_queue_.size;
  size_t data_len = packet.data.length();
  if (data_len > kMaxPacketDataSize) return Status::INVALID_ARGUMENT;

  // Prepare descriptor buffer (Prepend 10-byte VirtioNetHeader + Packet Data).
  uint8* tx_buf = (uint8*)tx_queue_.buffers_virt[desc_idx];
  memset(tx_buf, 0, kVirtioNetHeaderSize);
  memcpy(tx_buf + kVirtioNetHeaderSize, packet.data.data(), data_len);

  tx_queue_.desc[desc_idx].addr = tx_queue_.buffers_phys[desc_idx];
  tx_queue_.desc[desc_idx].len = kVirtioNetHeaderSize + data_len;
  tx_queue_.desc[desc_idx].flags = 0;  // Read-only by device.
  tx_queue_.desc[desc_idx].next = 0;

  // Make descriptor available.
  tx_queue_.avail->ring[tx_queue_.avail->idx % tx_queue_.size] = desc_idx;

  // Flush descriptors and available ring entries first.
  FlushRange(tx_queue_.mem, kQueueMemoryFlushSize);
  // Flush packet data payload as well.
  FlushRange(tx_buf, kVirtioNetHeaderSize + data_len);

  __asm__ __volatile__("" ::: "memory");
  tx_queue_.avail->idx++;
  __asm__ __volatile__("" ::: "memory");

  // Flush the updated index.
  FlushRange(tx_queue_.avail, kPageSize);

  // Notify queue 1 (TX).
  virtio_pci_.KickQueue(tx_queue_);

  return Status::OK;
}

Status VirtioNetworkDevice::SetPacketListener(
    const NetworkListener::Client& listener, ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanUseNetworkDevice))
    return Status::NOT_ALLOWED;

  listener_ = listener;
  return Status::OK;
}

Status VirtioNetworkDevice::SetMulticastFilter(const MulticastFilter& filter,
                                               ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanUseNetworkDevice))
    return Status::NOT_ALLOWED;

  std::lock_guard<std::mutex> lock(ctrl_mutex_);
  if (!has_ctrl_rx_) return Status::OK;

  bool all_multicast = filter.all_multicast ||
                       filter.addresses.size() > kMaxMulticastTableEntries;

  bool table_set = false;
  if (!all_multicast) {
    std::vector<const uint8*> multicast_macs;
    for (const MacAddress& address : filter.addresses)
      multicast_macs.push_back(address.mac);

    std::string tables;
    AppendMacTable(tables, {mac_});
    AppendMacTable(tables, multicast_macs);
    table_set = SendControlCommand(kVirtioNetCtrlMac, kVirtioNetCtrlMacTableSet,
                                   tables);
    if (!table_set) all_multicast = true;
  }

  if (!table_set) {
    std::string tables;
    AppendMacTable(tables, {mac_});
    AppendMacTable(tables, {});
    table_set = SendControlCommand(kVirtioNetCtrlMac, kVirtioNetCtrlMacTableSet,
                                   tables);
  }

  if (SetRxMode(kVirtioNetCtrlRxAllMulti, all_multicast) &&
      (!table_set || SetRxMode(kVirtioNetCtrlRxPromisc, false)))
    return Status::OK;

  // Keep receiving everything rather than risk dropping wanted frames.
  if (has_ctrl_rx_) (void)SetRxMode(kVirtioNetCtrlRxPromisc, true);
  return Status::INTERNAL_ERROR;
}

bool VirtioNetworkDevice::SetRxMode(uint8 command, bool enabled) {
  char on = enabled ? 1 : 0;
  return SendControlCommand(kVirtioNetCtrlRx, command,
                            std::string_view(&on, sizeof(on)));
}

bool VirtioNetworkDevice::SendControlCommand(uint8 command_class, uint8 command,
                                             std::string_view data) {
  if (kCtrlHeaderSize + data.size() > kCtrlAckOffset) return false;

  uint8* buffer = (uint8*)ctrl_queue_.buffers_virt[0];
  size_t buffer_phys = ctrl_queue_.buffers_phys[0];
  buffer[0] = command_class;
  buffer[1] = command;
  memcpy(buffer + kCtrlHeaderSize, data.data(), data.size());
  volatile uint8* ack = buffer + kCtrlAckOffset;
  *ack = kVirtioNetErr;

  ctrl_queue_.desc[kCtrlOutDescIndex].addr = buffer_phys;
  ctrl_queue_.desc[kCtrlOutDescIndex].len = kCtrlHeaderSize + data.size();
  ctrl_queue_.desc[kCtrlOutDescIndex].flags = kVringDescFNext;
  ctrl_queue_.desc[kCtrlOutDescIndex].next = kCtrlAckDescIndex;

  ctrl_queue_.desc[kCtrlAckDescIndex].addr = buffer_phys + kCtrlAckOffset;
  ctrl_queue_.desc[kCtrlAckDescIndex].len = kCtrlAckSize;
  ctrl_queue_.desc[kCtrlAckDescIndex].flags = kVringDescFWrite;
  ctrl_queue_.desc[kCtrlAckDescIndex].next = 0;

  ctrl_queue_.avail->ring[ctrl_queue_.avail->idx % ctrl_queue_.size] =
      kCtrlOutDescIndex;

  FlushRange(buffer, kPageSize);
  FlushRange(ctrl_queue_.mem, ctrl_queue_.mem_size);

  __asm__ __volatile__("" ::: "memory");
  ctrl_queue_.avail->idx++;
  __asm__ __volatile__("" ::: "memory");

  FlushRange(ctrl_queue_.mem, ctrl_queue_.mem_size);
  virtio_pci_.KickQueue(ctrl_queue_);

  for (int poll = 0; poll < kCtrlMaxPolls; poll++) {
    FlushRange(ctrl_queue_.mem, ctrl_queue_.mem_size);
    if (ctrl_queue_.used->idx != ctrl_queue_.last_seen_used) {
      ctrl_queue_.last_seen_used++;
      FlushRange(buffer, kPageSize);
      return *ack == kVirtioNetOk;
    }
    SleepForDuration(kCtrlPollInterval);
  }

  // The descriptors are still owned by the device, so never reuse them.
  std::cout << "VirtioNetworkDevice: Control command timed out." << std::endl;
  has_ctrl_rx_ = false;
  return false;
}

void VirtioNetworkDevice::HandleInterrupt() {
  if (processing_interrupt_) return;
  processing_interrupt_ = true;

  // Read ISR status (already read/cleared in kernel, but logs for info)
  if (virtio_pci_.io_base() != 0)
    (void)Read8BitsFromPort(virtio_pci_.io_base() + kVirtioPciIsr);

  while (true) {
    // Flush both virtual queues to ensure fresh used ring idx values are read
    // from physical RAM.
    FlushRange(rx_queue_.mem, kQueueMemoryFlushSize);
    FlushRange(tx_queue_.mem, kQueueMemoryFlushSize);

    // Reclaim finished transmit descriptors.
    {
      std::lock_guard<std::mutex> lock(tx_mutex_);
      tx_queue_.last_seen_used = tx_queue_.used->idx;
    }

    if (rx_queue_.last_seen_used == rx_queue_.used->idx) break;

    std::vector<Packet> packets_to_dispatch;
    uint16 new_idx = rx_queue_.avail->idx;

    while (rx_queue_.last_seen_used != rx_queue_.used->idx) {
      uint16 ring_idx = rx_queue_.last_seen_used % rx_queue_.size;
      uint32 desc_idx = rx_queue_.used->ring[ring_idx].id;
      uint32 len = std::min(
          static_cast<uint32>(rx_queue_.used->ring[ring_idx].len),
          kRxBufferSize);

      if (desc_idx >= rx_queue_.size || desc_idx >= kMaxQueueSize) {
        rx_queue_.last_seen_used++;
        continue;
      }

      // Skip the 10-byte VirtioNetHeader when unpacking packet payload.
      if (len > kVirtioNetHeaderSize) {
        Packet packet;
        packet.data = std::string(
            (const char*)rx_queue_.buffers_virt[desc_idx] + kVirtioNetHeaderSize,
            len - kVirtioNetHeaderSize);
        packets_to_dispatch.push_back(std::move(packet));
      }

      // Recycle descriptor slot back to available ring.
      rx_queue_.avail->ring[new_idx % rx_queue_.size] = desc_idx;
      new_idx++;

      rx_queue_.last_seen_used++;
    }

    // Flush RX Available ring changes (the ring entries, before updating idx).
    FlushRange(rx_queue_.avail,
               kAvailRingHeaderSize + rx_queue_.size * kAvailRingElementSize);

    __asm__ __volatile__("" ::: "memory");
    rx_queue_.avail->idx = new_idx;
    __asm__ __volatile__("" ::: "memory");

    // Flush RX Available ring index.
    FlushRange(rx_queue_.avail, kPageSize);

    // Notify queue 0 (RX) of newly available recycled descriptors.
    virtio_pci_.KickQueue(rx_queue_);

    for (const auto& packet : packets_to_dispatch) {
      if (listener_.IsValid()) (void)listener_.PacketReceived(packet);
    }
  }

  processing_interrupt_ = false;
}
