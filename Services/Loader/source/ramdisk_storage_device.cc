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

#include "ramdisk_storage_device.h"

#include <cstring>

#include "perception/memory.h"

namespace {

// Standard ISO 9660 sector size in bytes.
constexpr size_t kIsoSectorSize = 2048;

}  // namespace

RamdiskStorageDevice::RamdiskStorageDevice(
    std::unique_ptr<::perception::MultibootModule> module)
    : ::perception::devices::StorageDevice::Server(),
      module_(std::move(module)) {}

StatusOr<::perception::devices::StorageDeviceDetails>
RamdiskStorageDevice::GetDeviceDetails() {
  ::perception::devices::StorageDeviceDetails details;
  details.size_in_bytes = module_ ? module_->data.Length() : 0;
  details.is_writable = false;
  details.type = ::perception::devices::StorageDeviceType::RAM;
  details.name = "Ramdisk";
  details.optimal_operation_size = kIsoSectorSize;
  return details;
}

Status RamdiskStorageDevice::Read(
    const ::perception::devices::StorageDeviceReadRequest& request) {
  if (!module_)
    return Status::INTERNAL_ERROR;
  if (!request.buffer || !request.buffer->Join() ||
      **request.buffer == nullptr || !request.buffer->CanWrite())
    return Status::INVALID_ARGUMENT;

  size_t device_size = module_->data.Length();
  if (request.offset_on_device > device_size ||
      request.bytes_to_copy > device_size - request.offset_on_device)
    return Status::OVERFLOW;

  size_t buffer_size = request.buffer->GetSize();
  if (request.offset_in_buffer > buffer_size ||
      request.bytes_to_copy > buffer_size - request.offset_in_buffer)
    return Status::OVERFLOW;

  std::memcpy(
      reinterpret_cast<void*>(
          reinterpret_cast<size_t>(**request.buffer) + request.offset_in_buffer),
      reinterpret_cast<const void*>(
          reinterpret_cast<size_t>(*module_->data) + request.offset_on_device),
      request.bytes_to_copy);

  return Status::OK;
}

Status RamdiskStorageDevice::Write(
    const ::perception::devices::StorageDeviceWriteRequest& request) {
  return Status::NOT_ALLOWED;
}
