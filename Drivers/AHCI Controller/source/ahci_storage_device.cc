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
#include "perception/time.h"
#include "types.h"

using ::perception::AllocateMemoryPages;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::ReleaseMemoryPages;
using ::perception::Sleep;
using ::perception::devices::StorageDeviceDetails;
using ::perception::devices::StorageDeviceReadRequest;
using ::perception::devices::StorageDeviceType;
using ::perception::devices::StorageDeviceWriteRequest;

namespace {

// Size of the DMA bounce buffer in 4KB pages (128 pages = 512 KB).
constexpr size_t kDmaBufferPages = 128;

// Maximum byte count per AHCI PRDT entry (4MB).
constexpr size_t kMaxPrdtByteCount = 0x400000;

// Number of fast spin iterations before falling back to sleep.
constexpr int kFastSpinIterations = 100;

// Maximum number of sleep iterations before timing out a command.
constexpr int kMaxCompletionSleeps = 20000;

// Sleep interval during completion wait backoff.
constexpr auto kCompletionSleepInterval = std::chrono::microseconds(50);

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
                                     StorageDeviceType device_type)
    : ::perception::devices::StorageDevice::Server(
          {.defer_registration = true}),
      port_(port),
      port_index_(port_index),
      sector_count_(0),
      sector_size_(512),
      size_in_bytes_(0),
      device_type_(device_type),
      supports_lba48_(false) {
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

  // Allocate DMA transfer buffer (128KB = 32 pages)
  void* dma_virt = AllocateMemoryPages(kDmaBufferPages);
  std::memset(dma_virt, 0, kDmaBufferPages * kPageSize);
  dma_buffer_ = dma_virt;
  dma_buffer_phys_pages_.resize(kDmaBufferPages);
  for (size_t i = 0; i < kDmaBufferPages; i++) {
    dma_buffer_phys_pages_[i] = GetPhysicalAddressOfVirtualAddress(
        reinterpret_cast<size_t>(dma_virt) + i * kPageSize);
  }
}

bool AhciStorageDevice::Initialize() {
  StopPortCmd(port_);
  port_->clb = static_cast<uint32>(cmd_list_phys_ & 0xFFFFFFFF);
  port_->clbu = static_cast<uint32>((cmd_list_phys_ >> 32) & 0xFFFFFFFF);
  port_->fb = static_cast<uint32>(fis_base_phys_ & 0xFFFFFFFF);
  port_->fbu = static_cast<uint32>((fis_base_phys_ >> 32) & 0xFFFFFFFF);
  port_->serr = 0xFFFFFFFF;
  port_->is = 0xFFFFFFFF;
  StartPortCmd(port_);

  if (device_type_ == StorageDeviceType::OPTICAL) {
    if (!IdentifyAtapiDevice()) {
      StopPortCmd(port_);
      return false;
    }
  } else {
    if (!IdentifyAtaDevice()) {
      StopPortCmd(port_);
      return false;
    }
  }

  StartServing();
  return true;
}

void AhciStorageDevice::RecoverPortFromError() {
  StopPortCmd(port_);
  port_->serr = 0xFFFFFFFF;
  port_->is = 0xFFFFFFFF;
  StartPortCmd(port_);
}

bool AhciStorageDevice::IdentifyAtaDevice() {
  port_->is = 0xFFFFFFFF;

  std::memset(cmd_list_, 0, sizeof(HbaCmdHeader));
  cmd_list_[0].cfl = sizeof(FisRegH2D) / sizeof(uint32);
  cmd_list_[0].w = 0;
  cmd_list_[0].prdtl = 1;
  cmd_list_[0].ctba = static_cast<uint32>(cmd_tbl_phys_ & 0xFFFFFFFF);
  cmd_list_[0].ctbau = static_cast<uint32>((cmd_tbl_phys_ >> 32) & 0xFFFFFFFF);

  std::memset(cmd_tbl_, 0, sizeof(HbaCmdTbl));
  cmd_tbl_->prdt_entry[0].dba =
      static_cast<uint32>(dma_buffer_phys_pages_[0] & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbau =
      static_cast<uint32>((dma_buffer_phys_pages_[0] >> 32) & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbc = 512 - 1;
  cmd_tbl_->prdt_entry[0].i = 1;

  FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
  fis->fis_type = kFisTypeRegH2D;
  fis->pmport_c = 0x80;
  fis->command = kAtaCmdIdentifyDevice;

  int spin = 0;
  while ((port_->tfd & (0x80 | 0x08)) && spin < 1000000) {
    spin++;
  }
  if (port_->tfd & (0x80 | 0x08)) {
    std::cout << "IdentifyAtaDevice port busy! tfd=0x" << std::hex << port_->tfd
              << std::dec << std::endl;
    return false;
  }

  port_->ci = 1;

  spin = 0;
  constexpr int kMaxSpin = 10000000;
  while ((port_->ci & 1) != 0 && spin < kMaxSpin) {
    if (port_->is & (1 << 30)) {
      std::cout << "IdentifyAtaDevice task file error! is=0x" << std::hex
                << port_->is << " tfd=0x" << port_->tfd << std::dec
                << std::endl;
      RecoverPortFromError();
      return false;
    }
    asm volatile("pause");
    spin++;
  }
  if ((port_->ci & 1) != 0) {
    std::cout << "IdentifyAtaDevice timeout! ci=0x" << std::hex << port_->ci
              << " tfd=0x" << port_->tfd << " serr=0x" << port_->serr
              << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  port_->is = port_->is;

  if (port_->tfd & 0x01) {
    std::cout << "IdentifyAtaDevice error bit set! tfd=0x" << std::hex
              << port_->tfd << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  const uint8* raw_data = static_cast<const uint8*>(dma_buffer_);
  const uint16* words = reinterpret_cast<const uint16*>(raw_data);

  char model[41];
  for (int k = 0; k < 40; k += 2) {
    model[k] = raw_data[kAtaIdentModelOffset + k + 1];
    model[k + 1] = raw_data[kAtaIdentModelOffset + k];
  }
  model[40] = '\0';
  for (int k = 39; k >= 0; k--) {
    if (model[k] == ' ' || model[k] == '\0')
      model[k] = '\0';
    else
      break;
  }
  name_ = model[0] != '\0' ? std::string(model)
                           : "SATA Disk " + std::to_string(port_index_ + 1);

  bool lba48_supported =
      (words[83] & (1 << 10)) != 0 && (words[86] & (1 << 10)) != 0;
  if (lba48_supported) {
    sector_count_ =
        static_cast<uint64>(words[100]) |
        (static_cast<uint64>(words[101]) << 16) |
        (static_cast<uint64>(words[102]) << 32) |
        (static_cast<uint64>(words[103]) << 48);
    supports_lba48_ = true;
  } else {
    sector_count_ =
        static_cast<uint64>(words[60]) | (static_cast<uint64>(words[61]) << 16);
    supports_lba48_ = false;
  }

  sector_size_ = 512;
  if ((words[106] & 0xC000) == 0x4000 && (words[106] & (1 << 12)) != 0) {
    uint32 words_per_logical =
        static_cast<uint32>(words[117]) |
        (static_cast<uint32>(words[118]) << 16);
    if (words_per_logical > 0)
      sector_size_ = words_per_logical * 2;
  }

  size_in_bytes_ = sector_count_ * sector_size_;
  std::cout << "Detected " << name_ << " on port " << port_index_ << ": "
            << sector_count_ << " sectors (" << size_in_bytes_ / (1024 * 1024)
            << " MB), sector size " << sector_size_ << " bytes, "
            << (supports_lba48_ ? "48-bit LBA" : "28-bit LBA") << std::endl;
  return true;
}

bool AhciStorageDevice::IdentifyAtapiDevice() {
  port_->is = 0xFFFFFFFF;

  std::memset(cmd_list_, 0, sizeof(HbaCmdHeader));
  cmd_list_[0].cfl = sizeof(FisRegH2D) / sizeof(uint32);
  cmd_list_[0].w = 0;
  cmd_list_[0].prdtl = 1;
  cmd_list_[0].ctba = static_cast<uint32>(cmd_tbl_phys_ & 0xFFFFFFFF);
  cmd_list_[0].ctbau = static_cast<uint32>((cmd_tbl_phys_ >> 32) & 0xFFFFFFFF);

  std::memset(cmd_tbl_, 0, sizeof(HbaCmdTbl));
  cmd_tbl_->prdt_entry[0].dba =
      static_cast<uint32>(dma_buffer_phys_pages_[0] & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbau =
      static_cast<uint32>((dma_buffer_phys_pages_[0] >> 32) & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbc = 512 - 1;
  cmd_tbl_->prdt_entry[0].i = 1;

  FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
  fis->fis_type = kFisTypeRegH2D;
  fis->pmport_c = 0x80;
  fis->command = kAtaCmdIdentifyPacketDevice;

  int spin = 0;
  while ((port_->tfd & (0x80 | 0x08)) && spin < 1000000) {
    spin++;
  }

  if (!(port_->tfd & (0x80 | 0x08))) {
    port_->ci = 1;
    spin = 0;
    constexpr int kMaxSpin = 10000000;
    while ((port_->ci & 1) != 0 && spin < kMaxSpin) {
      if (port_->is & (1 << 30)) break;
      asm volatile("pause");
      spin++;
    }

    if ((port_->ci & 1) == 0 && !(port_->tfd & 0x01)) {
      const uint8* raw_data = static_cast<const uint8*>(dma_buffer_);
      char model[41];
      for (int k = 0; k < 40; k += 2) {
        model[k] = raw_data[kAtaIdentModelOffset + k + 1];
        model[k + 1] = raw_data[kAtaIdentModelOffset + k];
      }
      model[40] = '\0';
      for (int k = 39; k >= 0; k--) {
        if (model[k] == ' ' || model[k] == '\0')
          model[k] = '\0';
        else
          break;
      }
      if (model[0] != '\0') name_ = model;
    }
  }

  if (name_.empty())
    name_ = "SATA Optical Drive " + std::to_string(port_index_ + 1);

  port_->is = 0xFFFFFFFF;
  std::memset(cmd_list_, 0, sizeof(HbaCmdHeader));
  cmd_list_[0].cfl = sizeof(FisRegH2D) / sizeof(uint32);
  cmd_list_[0].a = 1;
  cmd_list_[0].w = 0;
  cmd_list_[0].prdtl = 1;
  cmd_list_[0].ctba = static_cast<uint32>(cmd_tbl_phys_ & 0xFFFFFFFF);
  cmd_list_[0].ctbau = static_cast<uint32>((cmd_tbl_phys_ >> 32) & 0xFFFFFFFF);

  std::memset(cmd_tbl_, 0, sizeof(HbaCmdTbl));
  cmd_tbl_->prdt_entry[0].dba =
      static_cast<uint32>(dma_buffer_phys_pages_[0] & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbau =
      static_cast<uint32>((dma_buffer_phys_pages_[0] >> 32) & 0xFFFFFFFF);
  cmd_tbl_->prdt_entry[0].dbc = 8 - 1;
  cmd_tbl_->prdt_entry[0].i = 1;

  fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
  fis->fis_type = kFisTypeRegH2D;
  fis->pmport_c = 0x80;
  fis->command = kAtaCmdPacket;
  fis->featurel = 0x05;
  fis->lba1 = 0xFF;
  fis->lba2 = 0xFF;

  uint8* acmd = cmd_tbl_->acmd;
  std::memset(acmd, 0, 16);
  acmd[0] = kAtapiCmdReadCapacity10;

  spin = 0;
  while ((port_->tfd & (0x80 | 0x08)) && spin < 1000000) {
    spin++;
  }

  bool capacity_succeeded = false;
  if (!(port_->tfd & (0x80 | 0x08))) {
    port_->ci = 1;
    spin = 0;
    constexpr int kMaxSpin = 10000000;
    while ((port_->ci & 1) != 0 && spin < kMaxSpin) {
      if (port_->is & (1 << 30)) break;
      asm volatile("pause");
      spin++;
    }

    if ((port_->ci & 1) == 0 && !(port_->tfd & 0x01)) {
      const uint8* data = static_cast<const uint8*>(dma_buffer_);
      uint32 last_lba = (static_cast<uint32>(data[0]) << 24) |
                        (static_cast<uint32>(data[1]) << 16) |
                        (static_cast<uint32>(data[2]) << 8) |
                        static_cast<uint32>(data[3]);
      uint32 block_length = (static_cast<uint32>(data[4]) << 24) |
                            (static_cast<uint32>(data[5]) << 16) |
                            (static_cast<uint32>(data[6]) << 8) |
                            static_cast<uint32>(data[7]);
      sector_count_ = static_cast<uint64>(last_lba) + 1;
      sector_size_ = block_length > 0 ? block_length : 2048;
      capacity_succeeded = true;
    }
  }

  if (!capacity_succeeded) {
    RecoverPortFromError();
    sector_count_ = 0;
    sector_size_ = 2048;
  }

  size_in_bytes_ = sector_count_ * sector_size_;
  std::cout << "Detected " << name_ << " on port " << port_index_ << ": "
            << sector_count_ << " sectors (" << size_in_bytes_ / (1024 * 1024)
            << " MB), sector size " << sector_size_ << " bytes" << std::endl;
  return true;
}


AhciStorageDevice::~AhciStorageDevice() {
  StopPortCmd(port_);
  if (cmd_list_) ReleaseMemoryPages(cmd_list_, 1);
  if (fis_base_) ReleaseMemoryPages(fis_base_, 1);
  if (cmd_tbl_) ReleaseMemoryPages(cmd_tbl_, 1);
  if (dma_buffer_) ReleaseMemoryPages(dma_buffer_, kDmaBufferPages);
}

StatusOr<StorageDeviceDetails> AhciStorageDevice::GetDeviceDetails() {
  StorageDeviceDetails details;
  details.size_in_bytes = size_in_bytes_;
  details.is_writable = (device_type_ != StorageDeviceType::OPTICAL);
  details.type = device_type_;
  details.name = name_;
  details.optimal_operation_size = kDmaBufferPages * kPageSize;
  return details;
}

void AhciStorageDevice::SetupDmaPrdt(size_t bytes_to_transfer, bool is_write) {
  std::memset(cmd_list_, 0, sizeof(HbaCmdHeader));
  cmd_list_[0].cfl = sizeof(FisRegH2D) / sizeof(uint32);
  cmd_list_[0].w = is_write ? 1 : 0;
  cmd_list_[0].ctba = static_cast<uint32>(cmd_tbl_phys_ & 0xFFFFFFFF);
  cmd_list_[0].ctbau = static_cast<uint32>((cmd_tbl_phys_ >> 32) & 0xFFFFFFFF);

  std::memset(cmd_tbl_, 0, sizeof(HbaCmdTbl));

  size_t num_pages = (bytes_to_transfer + kPageSize - 1) / kPageSize;
  size_t prdt_index = 0;
  size_t bytes_remaining = bytes_to_transfer;

  for (size_t p = 0; p < num_pages; ++p) {
    size_t chunk_size = std::min(kPageSize, bytes_remaining);
    size_t phys_addr = dma_buffer_phys_pages_[p];

    if (prdt_index > 0) {
      auto& prev = cmd_tbl_->prdt_entry[prdt_index - 1];
      uint64 prev_phys = static_cast<uint64>(prev.dba) |
                         (static_cast<uint64>(prev.dbau) << 32);
      size_t prev_size = prev.dbc + 1;
      if (prev_phys + prev_size == phys_addr &&
          prev_size + chunk_size <= kMaxPrdtByteCount) {
        prev.dbc += chunk_size;
        bytes_remaining -= chunk_size;
        continue;
      }
    }

    cmd_tbl_->prdt_entry[prdt_index].dba =
        static_cast<uint32>(phys_addr & 0xFFFFFFFF);
    cmd_tbl_->prdt_entry[prdt_index].dbau =
        static_cast<uint32>((phys_addr >> 32) & 0xFFFFFFFF);
    cmd_tbl_->prdt_entry[prdt_index].rsv0 = 0;
    cmd_tbl_->prdt_entry[prdt_index].dbc = static_cast<uint32>(chunk_size - 1);
    cmd_tbl_->prdt_entry[prdt_index].rsv1 = 0;
    cmd_tbl_->prdt_entry[prdt_index].i = 0;
    prdt_index++;

    bytes_remaining -= chunk_size;
  }

  if (prdt_index > 0)
    cmd_tbl_->prdt_entry[prdt_index - 1].i = 1;

  cmd_list_[0].prdtl = static_cast<uint16>(prdt_index);
}

bool AhciStorageDevice::PerformRead(uint64 start_sector, uint32 sector_count,
                                    void* buffer) {
  if (sector_count == 0) return true;
  if (start_sector + sector_count > sector_count_) return false;
  size_t bytes_to_read = sector_count * sector_size_;
  if (bytes_to_read > kDmaBufferPages * kPageSize) return false;

  port_->is = 0xFFFFFFFF;  // Clear interrupt status

  SetupDmaPrdt(bytes_to_read, /*is_write=*/false);

  if (device_type_ == StorageDeviceType::OPTICAL) {
    cmd_list_[0].a = 1;  // Set ATAPI bit in Command Header
    FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
    fis->fis_type = kFisTypeRegH2D;
    fis->pmport_c = 0x80;  // Command bit
    fis->command = kAtaCmdPacket;   // PACKET command (ATAPI)
    fis->featurel = 0x05;  // DMA mode (bit 0 = 1 for DMA transfer)
    fis->lba1 = 0xFF;      // Byte Count Limit Low (0xFFFF max)
    fis->lba2 = 0xFF;      // Byte Count Limit High

    // Setup ATAPI ACMD (READ 12 command)
    uint8* acmd = cmd_tbl_->acmd;
    std::memset(acmd, 0, 16);
    acmd[0] = kAtapiCmdRead12;
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

    if (supports_lba48_) {
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
    } else {
      fis->command = kAtaCmdReadDma;
      fis->lba0 = start_sector & 0xFF;
      fis->lba1 = (start_sector >> 8) & 0xFF;
      fis->lba2 = (start_sector >> 16) & 0xFF;
      fis->device = (1 << 6) | ((start_sector >> 24) & 0x0F);

      fis->countl = sector_count & 0xFF;
      fis->counth = 0;
    }
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

  // Wait for completion: fast spin phase
  spin = 0;
  while ((port_->ci & 1) != 0 && spin < kFastSpinIterations) {
    asm volatile("pause");
    spin++;
  }

  // Sleep backoff phase to avoid MMIO polling thrash in hypervisor
  int sleeps = 0;
  while ((port_->ci & 1) != 0 && sleeps < kMaxCompletionSleeps) {
    perception::SleepForDuration(kCompletionSleepInterval);
    sleeps++;
  }

  if ((port_->ci & 1) != 0) {
    std::cout << "PerformRead command timeout! ci=0x" << std::hex << port_->ci
              << " tfd=0x" << port_->tfd << " serr=0x" << port_->serr
              << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  if (port_->is & (1 << 30)) {  // Task file error
    std::cout << "PerformRead task file error! is=0x" << std::hex << port_->is
              << " tfd=0x" << port_->tfd << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  // Clear interrupt status bits
  port_->is = port_->is;

  if (port_->tfd & 0x01) {  // Error status bit
    std::cout << "PerformRead error status bit set! tfd=0x" << std::hex
              << port_->tfd << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  if (buffer != nullptr)
    std::memcpy(buffer, dma_buffer_, bytes_to_read);
  return true;
}

Status AhciStorageDevice::Read(const StorageDeviceReadRequest& request) {
  if (!request.buffer->Join()) return Status::INVALID_ARGUMENT;

  auto details = request.buffer->GetDetails();
  if (!details.CanWrite && !details.CanAssignPages)
    return Status::INVALID_ARGUMENT;

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
    void* batch_pages = AllocateMemoryPages(num_pages);
    if (batch_pages == nullptr) return Status::OUT_OF_MEMORY;

    for (size_t p = 0; p < num_pages; p++) {
      size_t page_index = start_page + p;
      size_t page_offset = page_index * kPageSize;
      void* new_page = static_cast<uint8*>(batch_pages) + p * kPageSize;
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

  const uint32 max_sectors_per_read = static_cast<uint32>(
      (kDmaBufferPages * kPageSize) / sector_size_);
  uint64 remaining_sectors = sector_count;
  uint64 current_sector = start_sector;
  uint64 bytes_copied = 0;

  while (remaining_sectors > 0) {
    uint32 chunk_sectors = static_cast<uint32>(
        std::min(static_cast<uint64>(max_sectors_per_read), remaining_sectors));

    if (!PerformRead(current_sector, chunk_sectors, nullptr)) {
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
        std::min(bytes_to_copy - bytes_copied, chunk_bytes_available);

    uint8* dest = get_virtual_address(buffer_offset + bytes_copied);
    std::memcpy(dest,
                static_cast<const uint8*>(dma_buffer_) + chunk_data_offset,
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

bool AhciStorageDevice::PerformWrite(uint64 start_sector, uint32 sector_count,
                                     const void* buffer) {
  if (sector_count == 0) return true;
  if (device_type_ == StorageDeviceType::OPTICAL) return false;
  if (start_sector + sector_count > sector_count_) return false;
  size_t bytes_to_write = sector_count * sector_size_;
  if (bytes_to_write > kDmaBufferPages * kPageSize) return false;

  if (buffer != nullptr)
    std::memcpy(dma_buffer_, buffer, bytes_to_write);

  port_->is = 0xFFFFFFFF;  // Clear interrupt status

  SetupDmaPrdt(bytes_to_write, /*is_write=*/true);

  // Setup FIS Reg H2D for ATA SATA Hard Disk Write DMA
  FisRegH2D* fis = reinterpret_cast<FisRegH2D*>(cmd_tbl_->cfis);
  fis->fis_type = kFisTypeRegH2D;
  fis->pmport_c = 0x80;  // Command bit

  if (supports_lba48_) {
    fis->command = kAtaCmdWriteDmaExt;
    fis->lba0 = start_sector & 0xFF;
    fis->lba1 = (start_sector >> 8) & 0xFF;
    fis->lba2 = (start_sector >> 16) & 0xFF;
    fis->device = (1 << 6);  // LBA mode

    fis->lba3 = (start_sector >> 24) & 0xFF;
    fis->lba4 = (start_sector >> 32) & 0xFF;
    fis->lba5 = (start_sector >> 40) & 0xFF;

    fis->countl = sector_count & 0xFF;
    fis->counth = (sector_count >> 8) & 0xFF;
  } else {
    fis->command = kAtaCmdWriteDma;
    fis->lba0 = start_sector & 0xFF;
    fis->lba1 = (start_sector >> 8) & 0xFF;
    fis->lba2 = (start_sector >> 16) & 0xFF;
    fis->device = (1 << 6) | ((start_sector >> 24) & 0x0F);

    fis->countl = sector_count & 0xFF;
    fis->counth = 0;
  }

  // Wait until port is not busy
  int spin = 0;
  while ((port_->tfd & (0x80 | 0x08)) && spin < 1000000) {
    spin++;
  }
  if (port_->tfd & (0x80 | 0x08)) {
    std::cout << "PerformWrite port busy! tfd=0x" << std::hex << port_->tfd
              << std::dec << std::endl;
    return false;
  }

  // Issue command slot 0
  port_->ci = 1;

  // Wait for completion: fast spin phase
  spin = 0;
  while ((port_->ci & 1) != 0 && spin < kFastSpinIterations) {
    asm volatile("pause");
    spin++;
  }

  // Sleep backoff phase to avoid MMIO polling thrash in hypervisor
  int sleeps = 0;
  while ((port_->ci & 1) != 0 && sleeps < kMaxCompletionSleeps) {
    perception::SleepForDuration(kCompletionSleepInterval);
    sleeps++;
  }

  if ((port_->ci & 1) != 0) {
    std::cout << "PerformWrite command timeout! ci=0x" << std::hex << port_->ci
              << " tfd=0x" << port_->tfd << " serr=0x" << port_->serr
              << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  if (port_->is & (1 << 30)) {  // Task file error
    std::cout << "PerformWrite task file error! is=0x" << std::hex
              << port_->is << " tfd=0x" << port_->tfd << std::dec
              << std::endl;
    RecoverPortFromError();
    return false;
  }

  // Clear interrupt status bits
  port_->is = port_->is;

  if (port_->tfd & 0x01) {  // Error status bit
    std::cout << "PerformWrite error status bit set! tfd=0x" << std::hex
              << port_->tfd << std::dec << std::endl;
    RecoverPortFromError();
    return false;
  }

  return true;
}

Status AhciStorageDevice::Write(const StorageDeviceWriteRequest& request) {
  if (device_type_ == StorageDeviceType::OPTICAL) return Status::NOT_ALLOWED;
  if (!request.buffer->Join()) return Status::INVALID_ARGUMENT;

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

  uint64 start_sector = device_offset_start / sector_size_;
  uint64 end_sector =
      (device_offset_start + bytes_to_copy + sector_size_ - 1) / sector_size_;
  uint32 sector_count = static_cast<uint32>(end_sector - start_sector);

  uint64 sector_offset_bytes = device_offset_start % sector_size_;

  const uint32 max_sectors_per_write = static_cast<uint32>(
      (kDmaBufferPages * kPageSize) / sector_size_);
  uint64 remaining_sectors = sector_count;
  uint64 current_sector = start_sector;
  uint64 bytes_written = 0;

  const uint8* src = (const uint8*)**request.buffer + buffer_offset;

  while (remaining_sectors > 0) {
    uint32 chunk_sectors = static_cast<uint32>(
        std::min(static_cast<uint64>(max_sectors_per_write), remaining_sectors));

    uint64 chunk_data_offset =
        (current_sector == start_sector) ? sector_offset_bytes : 0;
    uint64 chunk_bytes_available =
        (chunk_sectors * sector_size_) - chunk_data_offset;
    uint64 bytes_to_write_now =
        std::min(bytes_to_copy - bytes_written, chunk_bytes_available);

    if (chunk_data_offset > 0 ||
        bytes_to_write_now < (chunk_sectors * sector_size_)) {
      // Partial sector write: read existing into dma_buffer_, modify slice, write back
      if (!PerformRead(current_sector, chunk_sectors, nullptr))
        return Status::INTERNAL_ERROR;
      std::memcpy(static_cast<uint8*>(dma_buffer_) + chunk_data_offset,
                  src + bytes_written, bytes_to_write_now);
      if (!PerformWrite(current_sector, chunk_sectors, nullptr))
        return Status::INTERNAL_ERROR;
    } else {
      // Direct full sector write
      if (!PerformWrite(current_sector, chunk_sectors, src + bytes_written))
        return Status::INTERNAL_ERROR;
    }

    bytes_written += bytes_to_write_now;
    current_sector += chunk_sectors;
    remaining_sectors -= chunk_sectors;
  }

  return Status::OK;
}
