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

#include <string>
#include <vector>

#include "ahci_types.h"
#include "perception/devices/storage_device.h"
#include "types.h"

class AhciStorageDevice : public ::perception::devices::StorageDevice::Server {
 public:
  AhciStorageDevice(HbaPort* port, int port_index,
                    ::perception::devices::StorageDeviceType device_type =
                        ::perception::devices::StorageDeviceType::HARD_DRIVE);
  virtual ~AhciStorageDevice();

  // Initializes DMA structures, starts port, queries hardware identity/geometry,
  // and begins serving requests. Returns true if initialization succeeded.
  bool Initialize();

  StatusOr<::perception::devices::StorageDeviceDetails> GetDeviceDetails()
      override;

  Status Read(
      const ::perception::devices::StorageDeviceReadRequest& request) override;

  Status Write(
      const ::perception::devices::StorageDeviceWriteRequest& request) override;

  // Reads sectors into buffer using ATA or ATAPI DMA commands. If buffer is
  // null, data remains staged in dma_buffer_.
  bool PerformRead(uint64 start_sector, uint32 sector_count,
                   void* buffer = nullptr);

  // Writes sectors from buffer using ATA DMA commands. If buffer is null,
  // data is assumed to already be staged in dma_buffer_.
  bool PerformWrite(uint64 start_sector, uint32 sector_count,
                    const void* buffer = nullptr);

 private:
  HbaPort* port_;
  int port_index_;
  uint64 sector_count_;
  uint32 sector_size_;
  uint64 size_in_bytes_;
  std::string name_;
  ::perception::devices::StorageDeviceType device_type_;
  bool supports_lba48_;

  // Recovers the AHCI port after a Task File Error.
  void RecoverPortFromError();

  // Queries device geometry and model via ATA IDENTIFY DEVICE (0xEC).
  bool IdentifyAtaDevice();

  // Queries device model and media capacity via ATAPI IDENTIFY and READ CAPACITY.
  bool IdentifyAtapiDevice();

  // Sets up the PRDT entries and command header for a DMA transfer.
  void SetupDmaPrdt(size_t bytes_to_transfer, bool is_write);

  // DMA Memory Structures for slot 0
  HbaCmdHeader* cmd_list_;
  size_t cmd_list_phys_;

  void* fis_base_;
  size_t fis_base_phys_;

  HbaCmdTbl* cmd_tbl_;
  size_t cmd_tbl_phys_;

  void* dma_buffer_;
  std::vector<size_t> dma_buffer_phys_pages_;
};
