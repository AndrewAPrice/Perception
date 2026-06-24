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

#include "ide_storage_device.h"

#include <cstring>

#include "ide_types.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"

using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::kPageSize;
using ::perception::ReleaseMemoryPages;
using ::perception::Sleep;
using ::perception::WakeThread;
using ::perception::devices::StorageDeviceDetails;
using ::perception::devices::StorageDeviceReadRequest;
using ::perception::devices::StorageDeviceType;

IdeStorageDevice::IdeStorageDevice(IdeDevice* device, bool supports_dma)
    : ::perception::devices::StorageDevice::Server({.defer_registration = true}),
      device_(device),
      supports_dma_(supports_dma) {
  if (supports_dma_) {
    scratch_page_ = (unsigned char*)AllocateMemoryPagesBelowPhysicalAddressBase(
        17, 0xFFFFFFFF - 17 * kPageSize, scratch_page_physical_address_);
    if (scratch_page_) std::memset(scratch_page_, 0, 17 * kPageSize);
  } else {
    scratch_page_ = nullptr;
    scratch_page_physical_address_ = 0;
  }
  StartServing();
}

IdeStorageDevice::~IdeStorageDevice() {
  if (supports_dma_ && scratch_page_) {
    ReleaseMemoryPages(scratch_page_, 17);
  }
}

StatusOr<StorageDeviceDetails> IdeStorageDevice::GetDeviceDetails() {
  StorageDeviceDetails details;
  details.size_in_bytes = device_->size_in_bytes;
  details.is_writable = device_->is_writable;
  details.type = StorageDeviceType::OPTICAL;
  details.name = device_->name;
  details.optimal_operation_size =
      device_->size_in_bytes;  // Read full device or block size
  return details;
}

Status IdeStorageDevice::Read(const StorageDeviceReadRequest& request) {
  // Right now, join the memory buffer, but in the future it'll be nice to be
  // able to write without joining the memory buffer, which means if being able
  // to lock it without joining.
  if (!request.buffer->Join()) return Status::INVALID_ARGUMENT;

  auto details = request.buffer->GetDetails();
  if (!details.CanWrite && !details.CanAssignPages) {
    // Can't move the written data into this memory page.
    return Status::INVALID_ARGUMENT;
  }

  uint64 bytes_to_copy = request.bytes_to_copy;
  uint64 device_offset_start = request.offset_on_device;
  uint64 buffer_offset = request.offset_in_buffer;

  if (bytes_to_copy == 0) return Status::OK;  // Nothing to copy.

  if (device_offset_start + bytes_to_copy < device_offset_start ||
      device_offset_start + bytes_to_copy > device_->size_in_bytes)
    return Status::OVERFLOW;  // Reading beyond end of the device.

  if (buffer_offset + bytes_to_copy < buffer_offset ||
      buffer_offset + bytes_to_copy > request.buffer->GetSize())
    return Status::OVERFLOW;  // Writing beyond the end of the buffer.

  // Allocate request details on the fiber's stack.
  IdeRequest req;
  req.type = IdeRequestType::READ;
  req.master_drive = device_->master_drive;
  req.offset_on_device = device_offset_start;
  req.offset_in_buffer = buffer_offset;
  req.bytes_to_copy = bytes_to_copy;
  req.destination_buffer = (uint8*)**request.buffer;
  req.can_write = details.CanWrite;
  req.can_assign_pages = details.CanAssignPages;
  req.buffer_size = request.buffer->GetSize();
  req.shared_memory = request.buffer;
  req.completed.store(false, std::memory_order_release);
  req.fiber_to_wake = GetCurrentlyExecutingFiber();

  // Push request to the channel queue.
  auto* channel = device_->channel;
  if (!channel->queue.Push(&req)) return Status::OUT_OF_MEMORY;

  // Wake worker thread if it is asleep.
  if (channel->worker_thread_sleeping.load(std::memory_order_seq_cst))
    WakeThread(channel->worker_thread_id);

  // Yield the fiber until the request is processed by the worker thread.
  while (!req.completed.load(std::memory_order_acquire)) Sleep();

  return req.status;
}