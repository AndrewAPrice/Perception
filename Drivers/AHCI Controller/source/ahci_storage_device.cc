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

#include "ahci_storage_device.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"
#include "types.h"

using ::perception::AllocateMemoryPages;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::ReleaseMemoryPages;
using ::perception::Sleep;
using ::perception::devices::StorageDeviceDetails;
using ::perception::devices::StorageDeviceReadRequest;
using ::perception::devices::StorageDeviceType;

namespace {

void StopPortCmd(HbaPort* port) {
  port->cmd &= ~kAhciPortCmdSt;
  port->cmd &= ~kAhciPortCmdFre;

  int spin = 0;
  while (spin < 500000) {
    if ((port->cmd & kAhciPortCmdFr) == 0 && (port->cmd & kAhciPortCmdCr) == 0)
      break;
    spin++;
  }
}

void StartPortCmd(HbaPort* port) {
  int spin = 0;
  while ((port->cmd & kAhciPortCmdCr) != 0 && spin < 500000) {
    spin++;
  }
  port->cmd |= kAhciPortCmdFre;
  port->cmd |= kAhciPortCmdSt;
}

}  // namespace

AhciStorageDevice::AhciStorageDevice(HbaPort* port, int port_index,
                                     uint64 sector_count, uint32 sector_size,
                                     const std::string& name,
                                     StorageDeviceType device_type)
    : ::perception::devices::StorageDevice::Server(
          {.defer_registration = true}),
      port_(port),
      port_index_(port_index),
      sector_count_(sector_count),
      sector_size_(sector_size),
      size_in_bytes_(sector_count * sector_size),
      name_(name),
      device_type_(device_type) {
  // Allocate Command List (1KB)
  void* cl_virt = AllocateMemoryPages(1);
  std::memset(cl_virt, 0, kPageSize);
  cmd_list_ = static_cast<HbaCmdHeader*>(cl_virt);
  cmd_list_phys_ =
      GetPhysicalAddressOfVirtualAddress(reinterpret_cast<size_t>(cl_virt));

  // Allocate Received FIS (256B)
  void* fis_virt = AllocateMemoryPages(1);
  std::memset(fis_virt, 0, kPageSize);
  fis_base_ = fis_virt;
  fis_base_phys_ =
      GetPhysicalAddressOfVirtualAddress(reinterpret_cast<size_t>(fis_virt));

  // Allocate Command Table
  void* ct_virt = AllocateMemoryPages(1);
  std::memset(ct_virt, 0, kPageSize);
  cmd_tbl_ = static_cast<HbaCmdTbl*>(ct_virt);
  cmd_tbl_phys_ =
      GetPhysicalAddressOfVirtualAddress(reinterpret_cast<size_t>(ct_virt));

  // Allocate DMA transfer buffer (64KB = 16 pages)
  void* dma_virt = AllocateMemoryPages(16);
  std::memset(dma_virt, 0, 16 * kPageSize);
  dma_buffer_ = dma_virt;
  dma_buffer_phys_ =
      GetPhysicalAddressOfVirtualAddress(reinterpret_cast<size_t>(dma_virt));

  // Setup port registers
  StopPortCmd(port_);
  port_->clb = static_cast<uint32>(cmd_list_phys_ & 0xFFFFFFFF);
  port_->clbu = static_cast<uint32>((cmd_list_phys_ >> 32) & 0xFFFFFFFF);
  port_->fb = static_cast<uint32>(fis_base_phys_ & 0xFFFFFFFF);
  port_->fbu = static_cast<uint32>((fis_base_phys_ >> 32) & 0xFFFFFFFF);
  port_->serr = 0xFFFFFFFF;
  StartPortCmd(port_);

  StartServing();
}

AhciStorageDevice::~AhciStorageDevice() {
  StopPortCmd(port_);
  if (cmd_list_) ReleaseMemoryPages(cmd_list_, 1);
  if (fis_base_) ReleaseMemoryPages(fis_base_, 1);
  if (cmd_tbl_) ReleaseMemoryPages(cmd_tbl_, 1);
  if (dma_buffer_) ReleaseMemoryPages(dma_buffer_, 16);
}

StatusOr<StorageDeviceDetails> AhciStorageDevice::GetDeviceDetails() {
  StorageDeviceDetails details;
  details.size_in_bytes = size_in_bytes_;
  details.is_writable = (device_type_ != StorageDeviceType::OPTICAL);
  details.type = device_type_;
  details.name = name_;
  details.optimal_operation_size = sector_size_;
  return details;
}

bool AhciStorageDevice::PerformRead(uint64 start_sector, uint32 sector_count,
                                    void* buffer) {
  if (sector_count == 0) return true;
  size_t bytes_to_read = sector_count * sector_size_;
  if (bytes_to_read > 16 * kPageSize) return false;

  port_->is = 0xFFFFFFFF;  // Clear interrupt status

  // Setup Command Header 0
  std::memset(cmd_list_, 0, sizeof(HbaCmdHeader));
  cmd_list_[0].cfl = sizeof(FisRegH2D) / sizeof(uint32);
  cmd_list_[0].w = 0;  // Read from device
  cmd_list_[0].prdtl = 1;
  cmd_list_[0].ctba = static_cast<uint32>(cmd_tbl_phys_ & 0xFFFFFFFF);
  cmd_list_[0].ctbau = static_cast<uint32>((cmd_tbl_phys_ >> 32) & 0xFFFFFFFF);

  // Setup Command Table
  std::memset(cmd_tbl_, 0, sizeof(HbaCmdTbl));
  cmd_tbl_->prdt_entry[0].dba =
      static_cast<uint32>(dma_buffer_phys_ & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbau =
      static_cast<uint32>((dma_buffer_phys_ >> 32) & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbc = bytes_to_read - 1;
  cmd_tbl_->prdt_entry[0].i = 1;

  if (device_type_ == StorageDeviceType::OPTICAL) {
    cmd_list_[0].a = 1;  // Set ATAPI bit in Command Header
    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
    fis->fis_type = kFisTypeRegH2D;
    fis->pmport_c = 0x80;  // Command bit
    fis->command = 0xA0;   // PACKET command (ATAPI)
    fis->featurel = 0x05;  // DMA mode (bit 0 = 1 for DMA transfer)
    fis->lba1 = 0xFF;      // Byte Count Limit Low (0xFFFF max)
    fis->lba2 = 0xFF;      // Byte Count Limit High

    // Setup ATAPI ACMD (READ 12 command)
    uint8* acmd = cmd_tbl_->acmd;
    std::memset(acmd, 0, 16);
    acmd[0] = 0xA8;  // READ(12) opcode
    acmd[2] = (start_sector >> 24) & 0xFF;
    acmd[3] = (start_sector >> 16) & 0xFF;
    acmd[4] = (start_sector >> 8) & 0xFF;
    acmd[5] = start_sector & 0xFF;
    acmd[6] = (sector_count >> 24) & 0xFF;
    acmd[7] = (sector_count >> 16) & 0xFF;
    acmd[8] = (sector_count >> 8) & 0xFF;
    acmd[9] = sector_count & 0xFF;
  } else {
    // Setup FIS Reg H2D for ATA SATA Hard Disk
    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
    fis->fis_type = kFisTypeRegH2D;
    fis->pmport_c = 0x80;  // Command bit
    fis->command = kAtaCmdReadDmaExt;

    fis->lba0 = start_sector & 0xFF;
    fis->lba1 = (start_sector >> 8) & 0xFF;
    fis->lba2 = (start_sector >> 16) & 0xFF;
    fis->device = (1 << 6);  // LBA mode

    fis->lba3 = (start_sector >> 24) & 0xFF;
    fis->lba4 = (start_sector >> 32) & 0xFF;
    fis->lba5 = (start_sector >> 40) & 0xFF;

    fis->countl = sector_count & 0xFF;
    fis->counth = (sector_count >> 8) & 0xFF;
  }

  // Wait until port is not busy
  int spin = 0;
  while ((port_->tfd & (0x80 | 0x08)) && spin < 1000000) {
    spin++;
  }
  if (port_->tfd & (0x80 | 0x08)) {
    std::cout << "PerformRead port busy! tfd=0x" << std::hex << port_->tfd
              << std::dec << std::endl;
    return false;
  }

  // Issue command slot 0
  port_->ci = 1;

  // Wait for completion
  spin = 0;
  constexpr int kMaxSpin = 10000000;
  while ((port_->ci & 1) != 0 && spin < kMaxSpin) {
    if (port_->is & (1 << 30)) {  // Task file error
      std::cout << "PerformRead task file error! is=0x" << std::hex << port_->is
                << " tfd=0x" << port_->tfd << std::dec << std::endl;
      return false;
    }
    asm volatile("pause");
    spin++;
  }
  if ((port_->ci & 1) != 0) {
    std::cout << "PerformRead command timeout! ci=0x" << std::hex << port_->ci
              << " tfd=0x" << port_->tfd << " serr=0x" << port_->serr
              << std::dec << std::endl;
    return false;
  }

  // Clear interrupt status bits
  port_->is = port_->is;

  if (port_->tfd & 0x01) {  // Error status bit
    std::cout << "PerformRead error status bit set! tfd=0x" << std::hex
              << port_->tfd << std::dec << std::endl;
    return false;
  }

  std::memcpy(buffer, dma_buffer_, bytes_to_read);
  return true;
}

Status AhciStorageDevice::Read(const StorageDeviceReadRequest& request) {
  if (!request.buffer->Join()) return Status::INVALID_ARGUMENT;

  auto details = request.buffer->GetDetails();
  if (!details.CanWrite && !details.CanAssignPages) {
    return Status::INVALID_ARGUMENT;
  }

  uint64 bytes_to_copy = request.bytes_to_copy;
  uint64 device_offset_start = request.offset_on_device;
  uint64 buffer_offset = request.offset_in_buffer;

  if (bytes_to_copy == 0) return Status::OK;

  if (device_offset_start + bytes_to_copy < device_offset_start ||
      device_offset_start + bytes_to_copy > size_in_bytes_)
    return Status::OVERFLOW;

  if (buffer_offset + bytes_to_copy < buffer_offset ||
      buffer_offset + bytes_to_copy > request.buffer->GetSize())
    return Status::OVERFLOW;

  bool can_write = details.CanWrite && !details.IsLazilyAllocated;
  std::vector<void*> allocated_pages;
  size_t start_page = buffer_offset / kPageSize;
  size_t end_page = (buffer_offset + bytes_to_copy - 1) / kPageSize;
  size_t num_pages = end_page - start_page + 1;

  if (!can_write) {
    allocated_pages.resize(num_pages, nullptr);
    for (size_t p = 0; p < num_pages; p++) {
      size_t page_index = start_page + p;
      size_t page_offset = page_index * kPageSize;
      void* new_page = AllocateMemoryPages(1);
      allocated_pages[p] = new_page;
      if (request.buffer->IsPageAllocated(page_offset)) {
        std::memcpy(new_page, (uint8*)**request.buffer + page_offset,
                    kPageSize);
      } else {
        std::memset(new_page, 0, kPageSize);
      }
    }
  }

  auto get_virtual_address = [&](size_t offset) -> uint8* {
    if (can_write) {
      return (uint8*)**request.buffer + offset;
    } else {
      size_t page_index = offset / kPageSize;
      size_t offset_in_page = offset % kPageSize;
      size_t p = page_index - start_page;
      return (uint8*)allocated_pages[p] + offset_in_page;
    }
  };

  uint64 start_sector = device_offset_start / sector_size_;
  uint64 end_sector =
      (device_offset_start + bytes_to_copy + sector_size_ - 1) / sector_size_;
  uint32 sector_count = static_cast<uint32>(end_sector - start_sector);

  uint64 sector_offset_bytes = device_offset_start % sector_size_;

  // Maximum sectors per DMA chunk (1 sector = 2048 bytes, guaranteed physically
  // contiguous page)
  constexpr uint32 kMaxSectorsPerRead = 1;
  uint64 remaining_sectors = sector_count;
  uint64 current_sector = start_sector;
  uint64 bytes_copied = 0;

  std::vector<uint8> temp_buffer(kMaxSectorsPerRead * sector_size_);

  while (remaining_sectors > 0) {
    uint32 chunk_sectors = static_cast<uint32>(
        remaining_sectors < kMaxSectorsPerRead ? remaining_sectors
                                               : kMaxSectorsPerRead);

    if (!PerformRead(current_sector, chunk_sectors, temp_buffer.data())) {
      if (!can_write) {
        for (size_t p = 0; p < num_pages; p++) {
          if (allocated_pages[p]) ReleaseMemoryPages(allocated_pages[p], 1);
        }
      }
      return Status::INTERNAL_ERROR;
    }

    uint64 chunk_data_offset =
        (current_sector == start_sector) ? sector_offset_bytes : 0;
    uint64 chunk_bytes_available =
        (chunk_sectors * sector_size_) - chunk_data_offset;
    uint64 bytes_to_write_now =
        (bytes_to_copy - bytes_copied < chunk_bytes_available)
            ? (bytes_to_copy - bytes_copied)
            : chunk_bytes_available;

    uint8* dest = get_virtual_address(buffer_offset + bytes_copied);
    std::memcpy(dest, temp_buffer.data() + chunk_data_offset,
                bytes_to_write_now);

    bytes_copied += bytes_to_write_now;
    current_sector += chunk_sectors;
    remaining_sectors -= chunk_sectors;
  }

  if (!can_write) {
    for (size_t p = 0; p < num_pages; p++) {
      size_t page_index = start_page + p;
      request.buffer->AssignPage(allocated_pages[p], page_index * kPageSize);
    }
  }

  return Status::OK;
}
