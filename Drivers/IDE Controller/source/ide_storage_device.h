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

struct IdeDevice;

class IdeStorageDevice : public ::perception::devices::StorageDevice::Server {
 public:
  IdeStorageDevice(IdeDevice* device, bool supports_dma);
  virtual ~IdeStorageDevice();

  StatusOr<::perception::devices::StorageDeviceDetails> GetDeviceDetails()
      override;

  ::perception::Status Read(
      const ::perception::devices::StorageDeviceReadRequest& request) override;

  // Returns whether the device supports DMA.
  bool SupportsDma() const { return supports_dma_; }

  // Returns a pointer to the scratch page.
  unsigned char* GetScratchPage() const { return scratch_page_; }

  // Returns the physical address of the scratch page.
  size_t GetScratchPagePhysicalAddress() const {
    return scratch_page_physical_address_;
  }

 private:
  // The underlying physical IDE device.
  IdeDevice* device_;

  // Whether this device supports Bus Master DMA.
  bool supports_dma_;

  // Pointer to a 2-page memory area used for DMA support.
  // - Page 0: Stores the Physical Region Descriptor Table (PRDT) used by the
  // Bus Master DMA.
  // - Page 1: Acts as a scratch sector buffer for DMA transfers that cannot be
  // performed
  //   directly (e.g., due to misalignment, 64KB boundary crossing, or partial
  //   sector reads). It can hold up to 2 sectors (2048 bytes each).
  unsigned char* scratch_page_;

  // The physical address of the scratch_page_ allocation.
  size_t scratch_page_physical_address_;
};
