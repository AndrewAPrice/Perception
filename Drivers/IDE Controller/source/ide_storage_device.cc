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
#include <iostream>

#include "ata.h"
#include "ide.h"
#include "ide_types.h"
#include "interrupts.h"
#include "perception/memory.h"
#include "perception/port_io.h"
#include "perception/shared_memory.h"
#include "perception/threads.h"
#include "perception/time.h"

using ::perception::AllocateMemoryPages;
using ::perception::AllocateMemoryPagesBelowPhysicalAddressBase;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::kPageSize;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPort;
using ::perception::ReleaseMemoryPages;
using ::perception::SharedMemory;
using ::perception::SleepForDuration;
using ::perception::Status;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPort;
using ::perception::devices::StorageDeviceDetails;
using ::perception::devices::StorageDeviceReadRequest;
using ::perception::devices::StorageDeviceType;
using ::perception::devices::StorageDevice;

namespace {

constexpr size_t kMaxDmaBufferAddress = 0xFFFFFFFF - ATAPI_SECTOR_SIZE + 1;
// The optimal operation size, in bytes. The ATA PRDT has 512 entries. Each
// entry can read up to 64KiB, so the maximum size that can be read in a single
// DMA operation is 32MiB. But, the entries need to be physical contingious,
// which is functionaliy the kernel doesn't current support.
constexpr size_t kOptimalOperationSize = kPageSize * 512;

// The maximum number of sections that can be copied at once using DMA.
constexpr size_t kMaxDMASectorsAtOnce =
    kOptimalOperationSize / ATAPI_SECTOR_SIZE;

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

StatusOr<StorageDeviceDetails> IdeStorageDevice::GetDeviceDetails() {
  StorageDeviceDetails details;
  details.size_in_bytes = device_->size_in_bytes;
  details.is_writable = device_->is_writable;
  details.type = StorageDeviceType::OPTICAL;
  details.name = device_->name;
  details.optimal_operation_size = kOptimalOperationSize;
  return details;
}

Status IdeStorageDevice::Read(const StorageDeviceReadRequest& request) {
  // Right now, join the memory buffer, but in the future it'll be nice to be
  // able to write without joining the memory buffer, which means if being able
  // to lock it without joining.
  if (!request.buffer->Join()) {
    return ::perception::Status::INVALID_ARGUMENT;
  }

  auto details = request.buffer->GetDetails();
  if (!details.CanWrite && !details.CanAssignPages) {
    // Can't move the written data into this memory page.
    return ::perception::Status::INVALID_ARGUMENT;
  }

  int64 bytes_to_copy = request.bytes_to_copy;
  int64 device_offset_start = request.offset_on_device;
  int64 buffer_offset = request.offset_in_buffer;

  uint8* destination_buffer = (uint8*)**request.buffer;

  if (bytes_to_copy == 0) {
    // Nothing to copy.
    return Status::OK;
  }

  if (buffer_offset + bytes_to_copy > device_->size_in_bytes) {
    // Reading beyond end of the device.
    return Status::OVERFLOW;
  }

  if (buffer_offset + bytes_to_copy > request.buffer->GetSize()) {
    // Writing beyond the end of the buffer.
    return Status::OVERFLOW;
  }

  std::lock_guard<std::mutex> mutex(GetIdeMutex());

  IdeChannelRegisters* channel_registers =
      &device_->controller->channels[device_->primary_channel ? 0 : 1];
  // uint16 bus_master_id = channel_registers->bus_master_id;

  uint16 bus = device_->primary_channel ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;

  // Select drive - master/slave.
  SelectDriveOnBusIfNotSelected(device_->primary_channel,
                                device_->master_drive);

  size_t start_lba = device_offset_start / ATAPI_SECTOR_SIZE;
  size_t end_lba =
      (device_offset_start + bytes_to_copy - 1) / ATAPI_SECTOR_SIZE;

  // We read in entire sectors at a time, so if we don't want the data at the
  // start of the sector we need to skip some bytes.
  size_t skip_bytes = device_offset_start - (start_lba * ATAPI_SECTOR_SIZE);

  if (supports_dma_) {
    std::cout << "DMA is temporarily disabled until it is reimpmlemented.\n";
  } else {
    // Round up the bytes to sectors.
    size_t sectors_to_read = end_lba - start_lba + 1;
    RETURN_ON_ERROR(SentAtapiPacketCommand(bus, ATAPI_CMD_READ, start_lba,
                                           sectors_to_read));
    WaitForInterrupt(device_->primary_channel);

    size_t current_page_in_buffer = 0xFFFFFFFFFFFFFFFF;
    bool assign_page = 0;
    uint8* current_destination_page = nullptr;

    for (size_t i = 0; i < sectors_to_read; i++) {
      // Wait until ready
      uint8_t status = Read8BitsFromPort(ATA_COMMAND(bus));
      while (1) {
        uint8_t status = Read8BitsFromPort(ATA_COMMAND(bus));
        if (status & ATA_SR_ERR) {
          std::cout << "Device has en error.";
          return ::perception::Status::INTERNAL_ERROR;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
      }

      // The size of the data that's ready..
      int size = Read8BitsFromPort(ATA_ADDRESS3(bus)) << 8 |
                 Read8BitsFromPort(ATA_ADDRESS2(bus));

      // Read in the data from PIO, 2 bytes at a time.
      for (size_t j = 0; j < size; j += 2) {
        uint16 b = Read16BitsFromPort(ATA_DATA(bus));
        if (bytes_to_copy == 0) {
          // Skip these bytes because there's no more data we want to copy.
          continue;
        } else if (skip_bytes >= 2) {
          // Skip these bytes.
          skip_bytes -= 2;
          continue;
        }

        // The page index in the buffer.
        size_t buffer_page_index = buffer_offset / kPageSize;
        // Start of the buffer page.
        size_t buffer_page_start = buffer_page_index * kPageSize;
        // The offset in the page.
        size_t offset_in_buffer_page = buffer_offset - buffer_page_start;

        if (buffer_page_index != current_page_in_buffer) {
          // The page index in the buffer has changed.
          if (assign_page) {
            // Assign the previous page that was worked on.
            request.buffer->AssignPage(current_destination_page,
                                       current_page_in_buffer * kPageSize);
          }

          current_page_in_buffer = buffer_page_index;
          if (details.CanWrite) {
            // Can write directly into the destination buffer.
            current_destination_page = &destination_buffer[buffer_page_start];
            assign_page = false;
          } else {
            // Wwrite to a temp page that will be assigned into the shared
            // buffer.
            current_destination_page = (uint8*)AllocateMemoryPages(1);
            assign_page = true;
            // Will the buffer fill the entire page?
            bool will_fill_entire_memory =
                (offset_in_buffer_page == 0) && bytes_to_copy >= kPageSize;
            if (!will_fill_entire_memory) {
              // Does an old memory buffer exist?
              bool does_old_memory_exist =
                  request.buffer->IsPageAllocated(buffer_page_start);
              size_t after_last_byte = offset_in_buffer_page + bytes_to_copy;
              if (does_old_memory_exist) {
                // Copy the data around the region being read.
                // Before the data.
                if (offset_in_buffer_page > 0) {
                  memcpy(current_destination_page,
                         &destination_buffer[buffer_page_start],
                         offset_in_buffer_page);
                }
                // The data after.
                if (after_last_byte < kPageSize) {
                  memcpy(
                      &current_destination_page[after_last_byte],
                      &destination_buffer[buffer_page_start + after_last_byte],
                      kPageSize - after_last_byte);
                }

              } else {
                // Clear the data round the region being read.
                if (offset_in_buffer_page > 0)
                  memset(current_destination_page, 0, offset_in_buffer_page);
                if (after_last_byte < kPageSize) {
                  memset(&current_destination_page[after_last_byte], 0,
                         kPageSize - after_last_byte);
                }
              }
            }

            assign_page = true;
          }
        }

        if (skip_bytes == 1) {
          // Skip one byte and read just the last byte.
          current_destination_page[offset_in_buffer_page] = (b >> 8) & 0xFF;
          bytes_to_copy--;
          buffer_offset++;
        } else if (bytes_to_copy == 1) {
          // We only want one more byte so read just the first byte.
          current_destination_page[offset_in_buffer_page] = b & 0xFF;
          bytes_to_copy--;
          buffer_offset++;
        } else if (bytes_to_copy > 1) {
          // Copy both bytes.
          *(uint16*)&current_destination_page[offset_in_buffer_page] = b;
          bytes_to_copy -= 2;
          buffer_offset += 2;
        }
      }
    }

    if (assign_page) {
      // Assign the page that was just written.
      request.buffer->AssignPage(current_destination_page,
                                 current_page_in_buffer * kPageSize);
    }
  }

  /*
    for (size_t lba = start_lba; lba <= end_lba; lba++) {
      bool copy_into_dma_scratch_page = true;

      if (supports_dma_) {
        copy_into_dma_scratch_page = true;
        // Figure out if we can DMA directly into the destination buffer.
        if (bytes_to_copy >= ATAPI_SECTOR_SIZE &&
            IsRegionInTheSameMemoryPage(
                &destination_buffer[buffer_offset],
                &destination_buffer[buffer_offset + ATAPI_SECTOR_SIZE - 1])) {
          // The destination region fits into the same memory page and we're
          // reading a whole ATAPI sector. Therefore, we don't risk overriding
          // data in the destination buffer and we don't risk copying across
          // non-continguous physical address ranges.

          size_t physical_address_of_buffer =
    GetPhysicalAddressOfVirtualAddress(
              (size_t)&destination_buffer[buffer_offset]);
          if (physical_address_of_buffer <= kMaxDmaBufferAddress) {
            // The destination buffer fits in 32-bit memory, so we can use DMA
    to
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


      if (supports_dma_) {
        std::cout << "DMA oh no!" << std::endl;
        // Start!
        Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id),
                         ATA_BMR_COMMAND_START_BIT);

        WaitForInterrupt(device_->primary_channel);

        // Tell the bus to stop.
        Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), 0);

        // Need to read the status to flush the data to memory.
        uint8 status = Read8BitsFromPort(ATA_BMR_STATUS(bus_master_id));

        // Calculate how many bytes to read from this sector.
        size_t remaining_bytes_in_sector = (size_t)ATAPI_SECTOR_SIZE -
    skip_bytes; size_t bytes_read = std::min(remaining_bytes_in_sector,
    (size_t)bytes_to_copy);

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
      }
  */
  return Status::OK;
}

::perception::Status IdeStorageDevice::SentAtapiPacketCommand(
    uint16 bus, uint8 atapi_command, size_t lba, size_t num_sectors) {
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
  uint8 atapi_packet[12] = {atapi_command,
                            0,
                            uint8((lba >> 0x18) & 0xFF),
                            uint8((lba >> 0x10) & 0xFF),
                            uint8((lba >> 0x08) & 0xFF),
                            uint8((lba >> 0x00) & 0xFF),
                            uint8((num_sectors >> 0x18) & 0xFF),
                            uint8((num_sectors >> 0x10) & 0xFF),
                            uint8((num_sectors >> 0x08) & 0xFF),
                            uint8((num_sectors >> 0x00) & 0xFF),
                            0,
                            0};

  for (int byte = 0; byte < 12; byte += 2)
    Write16BitsToPort(ATA_DATA(bus), *(uint16*)&atapi_packet[byte]);

  return ::perception::Status::OK;
}