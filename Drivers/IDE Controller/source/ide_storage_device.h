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

#include "perception/devices/storage_device.h"

class IdeDevice;

class IdeStorageDevice : public ::perception::devices::StorageDevice::Server {
 public:
  IdeStorageDevice(IdeDevice* device, bool supports_dma);
  virtual ~IdeStorageDevice();

  StatusOr<::perception::devices::StorageDeviceDetails> GetDeviceDetails()
      override;

  ::perception::Status Read(
      const ::perception::devices::StorageDeviceReadRequest& request) override;

 private:
  IdeDevice* device_;

  // Does this device support Direct Memory Access?
  bool supports_dma_;

  // Scratch page for DMA and storing the Physical Region Descriptor Table.
  unsigned char* scratch_page_;

  // Physical address of the scratch page.
  size_t scratch_page_physical_address_;

  // Sends an ATAPI command.
  ::perception::Status SentAtapiPacketCommand(uint16 bus, uint8 atapi_command,
                                              size_t lba, size_t num_sectors);
};
