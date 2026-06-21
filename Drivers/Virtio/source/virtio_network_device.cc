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
#include "types.h"

// #define VERBOSE 1

using StatusOr;
using ::perception::AllocateMemoryPages;
using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::DoesProcessHavePermission;
using ::perception::FlushRange;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPciHdrBar0;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::kPciHdrCommandBitIoSpace;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read16BitsFromPort;
using ::perception::Read32BitsFromPciConfig;
using ::perception::Read32BitsFromPort;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Read8BitsFromPort;
using ::perception::RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort;
using ::perception::ReleaseMemoryPages;
using ::perception::Status;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPciConfig;
using ::perception::Write8BitsToPort;
using ::perception::devices::MacAddress;
using ::perception::devices::NetworkListener;
using ::perception::devices::Packet;
using ::perception::devices::PciDevice;

VirtioNetworkDevice::VirtioNetworkDevice(const PciDevice& device)
    : NetworkDevice::Server(), device_(device) {
  // Enable I/O space and Bus Master, clear Interrupt Disable in PCI config
  // space.
  uint8 cmd_low = Read8BitsFromPciConfig(device.bus, device.slot,
                                         device.function, kPciHdrCommand);
  uint8 cmd_high = Read8BitsFromPciConfig(device.bus, device.slot,
                                          device.function, kPciHdrCommand + 1);
  uint16 command = cmd_low | (cmd_high << 8);
  command |= kPciHdrCommandBitIoSpace | kPciHdrCommandBitBusMaster;
  command &= ~(1 << 10);  // Clear Interrupt Disable bit (bit 10)
  Write8BitsToPciConfig(device.bus, device.slot, device.function,
                        kPciHdrCommand, command & 0xFF);
  Write8BitsToPciConfig(device.bus, device.slot, device.function,
                        kPciHdrCommand + 1, (command >> 8) & 0xFF);

  uint16 cmd_check = Read16BitsFromPciConfig(device.bus, device.slot,
                                             device.function, kPciHdrCommand);

  // Retrieve BAR0 Base Address
  uint32 bar0 = Read32BitsFromPciConfig(device.bus, device.slot,
                                        device.function, kPciHdrBar0);
  io_base_ = bar0 & 0xFFFC;

  // Read Hardware MAC address from Virtio Configuration space (offset 20).
  for (int i = 0; i < 6; i++) mac_[i] = Read8BitsFromPort(io_base_ + 20 + i);
#if VERBOSE
  std::cout << "Hardware MAC Address: " << std::hex << (int)mac_[0] << ":"
            << (int)mac_[1] << ":" << (int)mac_[2] << ":" << (int)mac_[3] << ":"
            << (int)mac_[4] << ":" << (int)mac_[5] << std::dec << std::endl;
#endif

  // Perform Legacy Virtio Reset & Acknowledge handshake
  Write8BitsToPort(io_base_ + 18, 0);  // Reset
  Write8BitsToPort(io_base_ + 18,
                   Read8BitsFromPort(io_base_ + 18) | 1);  // ACKNOWLEDGE (1)
  Write8BitsToPort(io_base_ + 18,
                   Read8BitsFromPort(io_base_ + 18) | 2);  // DRIVER (2)

  // Negotiate features by reading offset 0 and writing 0 to offset 4 to
  // bypass modern features
  uint32 device_features = Read32BitsFromPort(io_base_ + 0);
  Write32BitsToPort(io_base_ + 4, 0);

  // Initialize Virtqueues (0 for RX, 1 for TX)
  rx_queue_.Setup(0, io_base_);
  tx_queue_.Setup(1, io_base_);

  // Set DRIVER_OK status bit
  Write8BitsToPort(io_base_ + 18,
                   Read8BitsFromPort(io_base_ + 18) | 4);  // DRIVER_OK (4)

  // Read the link status.
  (void)Read16BitsFromPort(io_base_ + 26);

  // Register Hardware Interrupt Handler using loop over port read to clear
  // ISR and prevent interrupt storm.
  uint8 irq =
      Read8BitsFromPciConfig(device.bus, device.slot, device.function, 0x3C);
  RegisterInterruptHandlerLoopOverStatusPortReadMaskedPort(
      irq, io_base_ + 19, 1, io_base_ + 19,
      [this](const uint8* bytes) { HandleInterrupt(); });
}

StatusOr<MacAddress> VirtioNetworkDevice::GetMacAddress() {
  MacAddress response;
  for (int i = 0; i < 6; i++) response.mac[i] = mac_[i];
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
  if (data_len > 4000) return Status::INVALID_ARGUMENT;

  // Prepare descriptor buffer (Prepend 10-byte VirtioNetHeader + Packet
  // Data).
  uint8* tx_buf = (uint8*)tx_queue_.buffers_virt[desc_idx];
  memset(tx_buf, 0, 10);  // Header flags and checksum zeroed out.
  memcpy(tx_buf + 10, packet.data.data(), data_len);

  tx_queue_.desc[desc_idx].addr = tx_queue_.buffers_phys[desc_idx];
  tx_queue_.desc[desc_idx].len = 10 + data_len;
  tx_queue_.desc[desc_idx].flags = 0;  // Read-only by device.
  tx_queue_.desc[desc_idx].next = 0;

  // Make descriptor available.
  tx_queue_.avail->ring[tx_queue_.avail->idx % tx_queue_.size] = desc_idx;
  __asm__ __volatile__("" ::: "memory");
  tx_queue_.avail->idx++;
  __asm__ __volatile__("" ::: "memory");

  // Flush entire 3 pages of Queue 1 (Descriptors, Available Ring, Used Ring).
  FlushRange(tx_queue_.mem, 12288);
  // Flush packet data payload as well.
  FlushRange(tx_buf, 10 + data_len);

  // Notify queue 1 (TX).
  Write16BitsToPort(io_base_ + 16, 1);

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
  uint8 isr = Read8BitsFromPort(io_base_ + 19);

  // Flush both virtual queues to ensure we read fresh Used ring idx values
  // from physical RAM!
  FlushRange(rx_queue_.mem, 12288);
  FlushRange(tx_queue_.mem, 12288);

  // Process received packets
  while (rx_queue_.last_seen_used != rx_queue_.used->idx) {
    uint16 ring_idx = rx_queue_.last_seen_used % rx_queue_.size;
    uint32 desc_idx = rx_queue_.used->ring[ring_idx].id;
    uint32 len = rx_queue_.used->ring[ring_idx].len;

    // Skip the 10-byte VirtioNetHeader when unpacking packet payload
    if (len > 10) {
      Packet packet;
      packet.data = std::string(
          (const char*)rx_queue_.buffers_virt[desc_idx] + 10, len - 10);

      if (listener_.IsValid()) {
        // Notify the listener that a packet is received. This must be
        // synchronous to avoid flooding it.
        (void)listener_.PacketReceived(packet);
      }
    }

    // Recycle descriptor slot back to available ring.
    rx_queue_.avail->ring[rx_queue_.avail->idx % rx_queue_.size] = desc_idx;
    rx_queue_.avail->idx++;

    rx_queue_.last_seen_used++;
  }

  // Flush RX Available ring changes.
  FlushRange(rx_queue_.avail, 6 + rx_queue_.size * 2);

  // Notify queue 0 (RX) of newly available recycled descriptors.
  Write16BitsToPort(io_base_ + 16, 0);

  // Reclaim finished transmit descriptors.
  tx_queue_.last_seen_used = tx_queue_.used->idx;

  processing_interrupt_ = false;
}

void VirtioNetworkDevice::QueueDetails::Setup(uint16 queue_idx,
                                              uint16 io_base) {
  // Select queue.
  Write16BitsToPort(io_base + 14, queue_idx);

  // Read queue size.
  uint16 qsize = Read16BitsFromPort(io_base + 12);

  // Calculate alignment and contiguous sizing for used ring allocation.
  size_t desc_table_size = qsize * 16;
  size_t avail_ring_size = 6 + qsize * 2;
  size_t used_ring_offset = (desc_table_size + avail_ring_size + 4095) & ~4095;
  size_t used_ring_size = 6 + qsize * 8;
  size_t total_size = used_ring_offset + used_ring_size;
  size_t pages = (total_size + 4095) / 4096;

  size_t physical_address = 0;
  void* virt_addr = nullptr;
  size_t phys0 = 0, phys1 = 0, phys2 = 0;

  while (true) {
    virt_addr = AllocateMemoryPagesBelowPhysicalAddressBase(pages, 0x0FFFFFFF,
                                                            physical_address);
    if (!virt_addr) return;

    phys0 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr);
    phys1 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr + 4096);
    phys2 = GetPhysicalAddressOfVirtualAddress((size_t)virt_addr + 8192);

    if ((phys1 == phys0 + 4096 && phys2 == phys1 + 4096) ||
        (phys1 == phys0 - 4096 && phys2 == phys1 - 4096)) {
      // Contiguous physical blocks confirmed!
      break;
    }

    // Non-contiguous pages, release and retry.
    ReleaseMemoryPages(virt_addr, pages);
  }

  memset(virt_addr, 0, pages * 4096);

  size = qsize;
  mem = virt_addr;
  phys = physical_address;
  last_seen_used = 0;

  // Resolve whether physical memory allocated grew upwards or downwards
  if (phys1 == phys0 + 4096 && phys2 == phys1 + 4096) {
    desc = (VirtQueueDesc*)virt_addr;
    avail = (VirtQueueAvail*)((size_t)virt_addr + desc_table_size);
    used = (VirtQueueUsed*)((size_t)virt_addr + used_ring_offset);
    physical_address = phys0;
  } else if (phys1 == phys0 - 4096 && phys2 == phys1 - 4096) {
    desc = (VirtQueueDesc*)((size_t)virt_addr + 8192);
    avail = (VirtQueueAvail*)((size_t)virt_addr + 4096);
    used = (VirtQueueUsed*)virt_addr;
    physical_address = phys2;
  } else {
    std::cout << "ERROR! Queue " << queue_idx
              << " physical pages are NOT contiguous!" << std::endl;
  }

  for (int i = 0; i < qsize; i++) {
    buffers_virt[i] = AllocateMemoryPages(1);
    buffers_phys[i] =
        GetPhysicalAddressOfVirtualAddress((size_t)buffers_virt[i]);
  }

  if (queue_idx == 0) {
    // Fill the receive descriptors with pre-allocated memory pages
    for (int i = 0; i < qsize; i++) {
      desc[i].addr = buffers_phys[i];
      desc[i].len = 4096;
      desc[i].flags = 2;  // Writable by device
      desc[i].next = 0;

      avail->ring[i] = i;
    }
    avail->flags = 0;
    __asm__ __volatile__("" ::: "memory");
    avail->idx = qsize;
    __asm__ __volatile__("" ::: "memory");
  } else {
    avail->flags = 0;
    avail->idx = 0;
  }

  // Flush entire Queue memory to physical RAM
  FlushRange(virt_addr, pages * 4096);

  Write32BitsToPort(io_base + 8, physical_address / 4096);

  if (queue_idx == 0) {
    __asm__ __volatile__("" ::: "memory");
    // Initial RX queue notification
    Write16BitsToPort(io_base + 16, 0);
  }
}