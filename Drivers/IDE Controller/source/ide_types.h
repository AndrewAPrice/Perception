// Copyright 2021 Google LLC
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

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "ide_storage_device.h"
#include "perception/lock_free_queue.h"
#include "perception/shared_memory.h"
#include "types.h"

// Represents the I/O and control port base addresses for a single IDE channel.
struct IdeChannelRegisters {
  // The base I/O port address for command/status registers (e.g., 0x1F0).
  uint16 io_base;

  // The base I/O port address for control/alternate status registers (e.g.,
  // 0x3F6).
  uint16 control_base;

  // The base port address for the channel's DMA bus master registers.
  uint16 bus_master_id;

  // Non-zero if interrupts are disabled for the channel.
  uint8 no_interrupt;
};

struct IdeDevice;

// The type of request sent to the channel's worker thread.
enum class IdeRequestType {
  // Probe for connected IDE/ATAPI devices.
  INITIALIZE,
  // Read data from a device.
  READ
};

// Represents a pending or in-progress read or initialization operation.
struct IdeRequest {
  // The type of request (INITIALIZE or READ).
  IdeRequestType type;

  // True if targeting the master drive, false for the slave drive.
  bool master_drive;

  // Read details.
  // The starting byte offset on the device.
  uint64 offset_on_device;

  // The starting byte offset in the destination buffer.
  uint64 offset_in_buffer;

  // The number of bytes to transfer.
  uint64 bytes_to_copy;

  // The raw virtual memory address of the destination buffer.
  uint8* destination_buffer;

  // True if the destination buffer can be written to directly.
  bool can_write;

  // True if pages can be reassigned to the shared memory buffer instead of
  // copying.
  bool can_assign_pages;

  // The total size of the destination buffer in bytes.
  size_t buffer_size;

  // The shared memory object representing the destination buffer.
  std::shared_ptr<::perception::SharedMemory> shared_memory;

  // Sync / Out.
  // Set to true when the request is fully processed.
  std::atomic<bool> completed;

  // The fiber to wake up when this request completes.
  ::perception::Fiber* fiber_to_wake;

  // The final status of the request execution.
  Status status;
};

// Represents a single IDE channel (Primary or Secondary).
struct IdeChannel {
  // The port registers mapped to this channel.
  IdeChannelRegisters registers;

  // Lock-free queue of requests to be processed by the worker thread.
  ::perception::LockFreeQueue<IdeRequest, 16> queue;

  // The thread ID of this channel's dedicated worker thread.
  ::perception::ThreadId worker_thread_id;

  // Atomic flag indicating if the worker thread is currently sleeping.
  std::atomic<bool> worker_thread_sleeping;

  // The fiber currently waiting for a hardware interrupt from this channel.
  std::atomic<::perception::Fiber*> waiting_on_interrupt;

  // True if an interrupt has been triggered.
  std::atomic<bool> interrupt_triggered;

  // The devices connected to this channel.
  std::vector<std::unique_ptr<IdeDevice>> devices;

  // True if this is the primary channel, false if it is the secondary channel.
  bool is_primary;

  // True if this channel supports Bus Master DMA.
  bool supports_dma;

  // The currently selected drive on the channel (0 = master, 1 = slave, -1 =
  // none).
  int selected_drive = -1;
};

// Represents a physical device connected to an IDE channel.
struct IdeDevice {
  // True if this device is the master drive on the channel, false if it is the
  // slave.
  bool master_drive;

  // The type of the device (e.g., IDE_ATA or IDE_ATAPI).
  uint16 type;

  // The device signature retrieved during identification.
  uint16 signature;

  // Device capabilities flags from the IDENTIFY command.
  uint16 capabilities;

  // Supported command sets bitmap from the IDENTIFY command.
  uint32 command_sets;

  // Device capacity in sectors.
  uint32 size;

  // Device capacity in bytes.
  uint64 size_in_bytes;

  // Whether the device supports write operations.
  bool is_writable;

  // The model name/number of the device.
  std::string name;

  // Pointer to the IDE channel this device is connected to.
  IdeChannel* channel;

  // High-level storage device wrapper registered with the Storage Manager.
  std::unique_ptr<IdeStorageDevice> storage_device;
};

// Represents the IDE controller, containing primary and secondary channels.
struct IdeController {
  // The channels managed by this controller (index 0 is Primary, index 1 is
  // Secondary).
  IdeChannel channels[2];
};
