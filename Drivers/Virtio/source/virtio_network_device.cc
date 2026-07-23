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

#include <cstring>
#include <iostream>
#include <mutex>

#include "perception/cache.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/network_device.h"
#include "perception/interrupts.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/permissions.h"
#include "perception/port_io.h"
#include "queue.h"
#include "types.h"
#include "virtio.h"

// #define VERBOSE 1

using ::perception::DoesProcessHavePermission;
using ::perception::FlushRange;
using ::perception::kPageSize;
using ::perception::kPciHdrBar0;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPort;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::Write16BitsToPort;
using ::perception::devices::MacAddress;
using ::perception::devices::NetworkListener;
using ::perception::devices::Packet;
using ::perception::devices::PciDevice;

namespace {

constexpr uint32 kIoBaseMask = 0xFFFC;
constexpr size_t kMacAddressLength = 6;
constexpr uint16 kVirtioNetConfigMacOffset = 20;
constexpr uint16 kVirtioNetConfigStatusOffset = 26;
constexpr uint16 kRxQueueIndex = 0;
constexpr uint16 kTxQueueIndex = 1;
constexpr uint32 kRxBufferSize = 4096;
constexpr uint16 kVringDescFWrite = 2;
constexpr size_t kVirtioNetHeaderSize = 10;
constexpr size_t kMaxPacketDataSize = 4000;
constexpr size_t kQueueMemoryFlushSize = 12288;
constexpr size_t kAvailRingHeaderSize = 6;
constexpr size_t kAvailRingElementSize = 2;
constexpr uint8 kIsrReadMask = 1;

}  // namespace

VirtioNetworkDevice::VirtioNetworkDevice(const PciDevice& device)
    : NetworkDevice::Server({.defer_registration = true}), device_(device) {
  EnableVirtioPciDevice(device);

  // Retrieve BAR0 Base Address
  uint32 bar0 = Read32BitsFromPciConfig(device.bus, device.slot,
                                        device.function, kPciHdrBar0);
  io_base_ = bar0 & kIoBaseMask;

  // Read Hardware MAC address from Virtio Configuration space (offset 20).
  for (int i = 0; i < kMacAddressLength; i++) {
    mac_[i] = Read8BitsFromPort(io_base_ + kVirtioNetConfigMacOffset + i);
  }
#if VERBOSE
  std::cout << "Hardware MAC Address: " << std::hex << (int)mac_[0] << ":"
            << (int)mac_[1] << ":" << (int)mac_[2] << ":" << (int)mac_[3] << ":"
            << (int)mac_[4] << ":" << (int)mac_[5] << std::dec << std::endl;
#endif

  // Perform Legacy Virtio Reset & Acknowledge handshake
  ResetLegacyVirtioDevice(io_base_);

  // Initialize Virtqueues (0 for RX, 1 for TX)
  rx_queue_.Setup(kRxQueueIndex, io_base_);
  tx_queue_.Setup(kTxQueueIndex, io_base_);
  if (!rx_queue_.desc || !rx_queue_.avail || !tx_queue_.desc || !tx_queue_.avail) {
    std::cout << "VirtioNetworkDevice: Virtqueue setup failed!" << std::endl;
    return;
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

  FlushRange((void*)rx_queue_.avail, kPageSize);

  // Initial RX queue notification
  Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, kRxQueueIndex);

  // Set DRIVER_OK status bit
  SetVirtioDriverOk(io_base_);

  // Read the link status.
  (void)Read16BitsFromPort(io_base_ + kVirtioNetConfigStatusOffset);

  // Register Hardware Interrupt Handler using loop over port read to clear
  // ISR and prevent interrupt storm.
  uint8 irq = GetPciInterruptLine(device);
  RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
      irq, io_base_ + kVirtioPciIsr, kIsrReadMask, io_base_ + kVirtioPciIsr,
      [this](const uint8* bytes) { HandleInterrupt(); });

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

  // Prepare descriptor buffer (Prepend 10-byte VirtioNetHeader + Packet
  // Data).
  uint8* tx_buf = (uint8*)tx_queue_.buffers_virt[desc_idx];
  memset(tx_buf, 0,
         kVirtioNetHeaderSize);  // Header flags and checksum zeroed out.
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
  Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, kTxQueueIndex);

  return Status::OK;
}

Status VirtioNetworkDevice::SetPacketListener(
    const NetworkListener::Client& listener, ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanUseNetworkDevice))
    return Status::NOT_ALLOWED;

  listener_ = listener;
  return Status::OK;
}

void VirtioNetworkDevice::HandleInterrupt() {
  if (processing_interrupt_) return;
  processing_interrupt_ = true;

  // Read ISR status (already read/cleared in kernel, but logs for info)
  uint8 isr = Read8BitsFromPort(io_base_ + kVirtioPciIsr);

  // Flush both virtual queues to ensure we read fresh Used ring idx values
  // from physical RAM!
  FlushRange(rx_queue_.mem, kQueueMemoryFlushSize);
  FlushRange(tx_queue_.mem, kQueueMemoryFlushSize);

  // Process received packets
  uint16 new_idx = rx_queue_.avail->idx;
  while (rx_queue_.last_seen_used != rx_queue_.used->idx) {
    uint16 ring_idx = rx_queue_.last_seen_used % rx_queue_.size;
    uint32 desc_idx = rx_queue_.used->ring[ring_idx].id;
    uint32 len = rx_queue_.used->ring[ring_idx].len;

    // Skip the 10-byte VirtioNetHeader when unpacking packet payload
    if (len > kVirtioNetHeaderSize) {
      Packet packet;
      packet.data = std::string(
          (const char*)rx_queue_.buffers_virt[desc_idx] + kVirtioNetHeaderSize,
          len - kVirtioNetHeaderSize);

      if (listener_.IsValid()) {
        // Notify the listener that a packet is received. This must be
        // synchronous to avoid flooding it.
        (void)listener_.PacketReceived(packet);
      }
    }

    // Recycle descriptor slot back to available ring.
    rx_queue_.avail->ring[new_idx % rx_queue_.size] = desc_idx;
    new_idx++;

    rx_queue_.last_seen_used++;
  }

  // Flush RX Available ring changes (the ring entries, before we update idx).
  FlushRange(rx_queue_.avail,
             kAvailRingHeaderSize + rx_queue_.size * kAvailRingElementSize);

  __asm__ __volatile__("" ::: "memory");
  rx_queue_.avail->idx = new_idx;
  __asm__ __volatile__("" ::: "memory");

  // Flush RX Available ring index.
  FlushRange(rx_queue_.avail, kPageSize);

  // Notify queue 0 (RX) of newly available recycled descriptors.
  Write16BitsToPort(io_base_ + kVirtioPciQueueNotify, kRxQueueIndex);

  // Reclaim finished transmit descriptors.
  tx_queue_.last_seen_used = tx_queue_.used->idx;

  processing_interrupt_ = false;
}
