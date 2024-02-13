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

#include <math.h>

#include "ata.h"
#include "ide.h"
#include "ide_types.h"
#include "interrupts.h"
#include "perception/memory.h"
#include "perception/port_io.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"
#include "perception/time.h"

using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPort;
using ::perception::ReleaseMemoryPages;
using ::perception::SharedMemory;
using ::perception::SleepForDuration;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPort;
using ::permebuf::perception::devices::StorageDevice;
using ::permebuf::perception::devices::StorageType;

namespace {

constexpr size_t kMaxDmaBufferAddress = 0xFFFFFFFF - ATAPI_SECTOR_SIZE + 1;

// Offset, in bytes, into the scratch buffer of where the drive should write to
// during DMA. The scratch buffer is also used to store the Physical Region
// Descriptor Table. The PRDT contains 1 entry that is 8 bytes long.
constexpr size_t kScratchBufferTempDataOffset = 8;

bool IsRegionInTheSameMemoryPage(void* min, void* max) {
  size_t min_page = (size_t)min / kPageSize;
  size_t max_page = (size_t)max / kPageSize;
  return min_page == max_page;
}

}  // namespace

IdeStorageDevice::IdeStorageDevice(IdeDevice* device, bool supports_dma)
    : device_(device), supports_dma_(supports_dma) {
  if (supports_dma_) {
    // The scratch page needs to live in 32-bit physical memory.
    scratch_page_ = (unsigned char*)AllocateMemoryPagesBelowPhysicalAddressBase(
        1, 0xFFFFFFFF - kPageSize, scratch_page_physical_address_);

    // Construct part of the physical region descriptor table that never
    // changes.
    // Size to copy.
    *((uint16*)&scratch_page_[4]) = ATAPI_SECTOR_SIZE;
    // Set flag to indicates this is the only entry in the PTR.
    *((uint16*)&scratch_page_[6]) = 1 << 15;
  }
}

IdeStorageDevice::~IdeStorageDevice() {
  if (supports_dma_) {
    ReleaseMemoryPages(scratch_page_, 1);
  }
}

StatusOr<Permebuf<StorageDevice::GetDeviceDetailsResponse>>
IdeStorageDevice::HandleGetDeviceDetails(
    ::perception::ProcessId sender,
    const StorageDevice::GetDeviceDetailsRequest& request) {
  Permebuf<StorageDevice::GetDeviceDetailsResponse> response;
  response->SetSizeInBytes(device_->size_in_bytes);
  response->SetIsWritable(device_->is_writable);
  response->SetType(StorageType::Optical);
  response->SetName(device_->name);
  return response;
}

StatusOr<StorageDevice::ReadResponse> IdeStorageDevice::HandleRead(
    ::perception::ProcessId sender, const StorageDevice::ReadRequest& request) {
  SharedMemory destination_shared_memory = request.GetBuffer();
  if (!destination_shared_memory.Join() ||
      destination_shared_memory.IsLazilyAllocated() ||
      !destination_shared_memory.CanWrite()) {
    return ::perception::Status::INVALID_ARGUMENT;
  }

  int64 bytes_to_copy = request.GetBytesToCopy();
  int64 device_offset_start = request.GetOffsetOnDevice();
  int64 buffer_offset = request.GetOffsetInBuffer();

  uint8* destination_buffer = (uint8*)*destination_shared_memory;

  if (bytes_to_copy == 0) {
    // Nothing to copy.
    return StorageDevice::ReadResponse();
  }

  if (buffer_offset + bytes_to_copy > device_->size_in_bytes) {
    // Reading beyond end of the device.
    return ::perception::Status::OVERFLOW;
  }

  if (buffer_offset + bytes_to_copy > destination_shared_memory.GetSize()) {
    // Writing beyond the end of the buffer.
    return ::perception::Status::OVERFLOW;
  }

  std::lock_guard<std::mutex> mutex(GetIdeMutex());

  IdeChannelRegisters* channel_registers =
      &device_->controller->channels[device_->primary_channel ? 0 : 1];
  uint16 bus_master_id = channel_registers->bus_master_id;

  // Select drive - master/slave.
  uint16 bus = device_->primary_channel ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
  SelectDriveOnBusIfNotSelected(device_->primary_channel,
                                device_->master_drive);

  size_t start_lba = device_offset_start / ATAPI_SECTOR_SIZE;
  size_t end_lba =
      (device_offset_start + bytes_to_copy + ATAPI_SECTOR_SIZE - 1) /
      ATAPI_SECTOR_SIZE;

  // We read in entire sectors at a time, so if we don't want the data at the
  // start of the sector we need to skip some bytes.
  size_t skip_bytes = device_offset_start - (start_lba * ATAPI_SECTOR_SIZE);

  for (size_t lba = start_lba; lba <= end_lba; lba++) {
    bool copy_into_dma_scratch_page = true;

    if (supports_dma_) {
      // Figure out if we can DMA directly into the destination buffer.
      if (bytes_to_copy >= ATAPI_SECTOR_SIZE &&
          IsRegionInTheSameMemoryPage(
              &destination_buffer[buffer_offset],
              &destination_buffer[buffer_offset + ATAPI_SECTOR_SIZE - 1])) {
        // The destination region fits into the same memory page and we're
        // reading a whole ATAPI sector. Therefore, we don't risk overriding
        // data in the destination buffer and we don't risk copying across
        // non-continguous physical address ranges.

        size_t physical_address_of_buffer = GetPhysicalAddressOfVirtualAddress(
            (size_t)&destination_buffer[buffer_offset]);
        if (physical_address_of_buffer <= kMaxDmaBufferAddress) {
          // The destination buffer fits in 32-bit memory, so we can use DMA to
          // write directly into it.
          copy_into_dma_scratch_page = false;
          // Set the destination to be the buffer.
          *((uint32*)scratch_page_) = (uint32)physical_address_of_buffer;
        }
      }

      if (copy_into_dma_scratch_page) {
        // The destination buffer failed the above checks, so we need to copy
        // into the scratch buffer, then once the data is ready, copy from the
        // scratch buffer into the destination buffer.

        *((uint32*)scratch_page_) = (uint32)scratch_page_physical_address_ +
                                    kScratchBufferTempDataOffset;
      }

      // Clear the command bits.
      Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), 0);

      // Clear the status.
      Write8BitsToPort(ATA_BMR_STATUS(bus_master_id), 0);

      // Set the PRDT into the Bus Master Register.
      Write32BitsToPort(ATA_BMR_PRDT(bus_master_id),
                        (size_t)scratch_page_physical_address_);
    }

    // Send packet command.
    Write8BitsToPort(ATA_COMMAND(bus), ATA_CMD_PACKET);

    ResetInterrupt(device_->primary_channel);

    // Poll while busy.
    uint8 status = Read8BitsFromPort(ATA_COMMAND(bus));
    while ((status & ATA_SR_BSY) ||
           !(status & (ATA_REG_SECCOUNT1 | ATA_REG_ERROR))) {
      SleepForDuration(std::chrono::milliseconds(10));
      status = Read8BitsFromPort(ATA_COMMAND(bus));
    }

    // is there an error?
    if (status & ATA_REG_ERROR) return ::perception::Status::MISSING_MEDIA;

    // Send the ATAPI packet, which is 6 words / 12 bytes long.
    uint8 atapi_packet[12] = {ATAPI_CMD_READ,
                              0,
                              uint8((lba >> 0x18) & 0xFF),
                              uint8((lba >> 0x10) & 0xFF),
                              uint8((lba >> 0x08) & 0xFF),
                              uint8((lba >> 0x00) & 0xFF),
                              0,
                              0,
                              0,
                              1,
                              0,
                              0};

    for (status = 0; status < 12; status += 2) {
      Write16BitsToPort(ATA_DATA(bus), *(uint16*)&atapi_packet[status]);
    }

    if (supports_dma_) {
      // Start!
      Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id),
                       ATA_BMR_COMMAND_START_BIT);

      WaitForInterrupt(device_->primary_channel);

      // Tell the bus to stop.
      Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), 0);

      // Need to read the status to flush the data to memory.
      uint8 status = Read8BitsFromPort(ATA_BMR_STATUS(bus_master_id));

      // Calculate how many bytes to read from this sector.
      size_t remaining_bytes_in_sector = (size_t)ATAPI_SECTOR_SIZE - skip_bytes;
      size_t bytes_read =
          std::min(remaining_bytes_in_sector, (size_t)bytes_to_copy);

      if (copy_into_dma_scratch_page) {
        // Copy the data out of the scratch buffer and into the destination
        // buffer.
        memcpy(&destination_buffer[buffer_offset],
               &scratch_page_[kScratchBufferTempDataOffset + skip_bytes],
               bytes_read);
      }
      bytes_to_copy -= bytes_read;
      buffer_offset += bytes_read;
      skip_bytes = 0;
    } else {
      WaitForInterrupt(device_->primary_channel);
      // Read in the data from PIO.
      size_t indx = lba * ATAPI_SECTOR_SIZE;
      for (size_t i = 0; i < ATAPI_SECTOR_SIZE; i += 2) {
        uint16 b = Read16BitsFromPort(ATA_DATA(bus));

        if (bytes_to_copy == 0) {
          // Skip these bytes because there's no more data we want to copy.
          continue;
        } else if (skip_bytes >= 2) {
          // Skip these bytes.
          skip_bytes -= 2;
          continue;
        } else if (skip_bytes == 1) {
          // Skip one byte and read just the last byte.
          destination_buffer[buffer_offset] = (b > 8) & 0xFF;
          bytes_to_copy--;
          buffer_offset++;
        } else if (bytes_to_copy == 1) {
          // We only want one more byte so read just the first byte.
          destination_buffer[buffer_offset] = b & 0xFF;
          bytes_to_copy--;
          buffer_offset++;
        } else if (bytes_to_copy > 1) {
          // Copy both bytes.
          *(uint16*)&destination_buffer[buffer_offset] = b;
          bytes_to_copy -= 2;
          buffer_offset += 2;
        }
      }
    }
  }
  return StorageDevice::ReadResponse();
}