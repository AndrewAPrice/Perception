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

#include "ide_worker.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "ata.h"
#include "ide_types.h"
#include "interrupts.h"
#include "io.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/port_io.h"
#include "perception/threads.h"
#include "perception/time.h"

using ::perception::AllocateMemoryPages;
using ::perception::GetPhysicalAddressOfVirtualAddress;
using ::perception::GetThreadId;
using ::perception::kPageSize;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPort;
using ::perception::ReleaseMemoryPages;
using ::perception::SetThreadPriority;
using ::perception::SleepForDuration;
using ::perception::SleepThisThread;
using ::perception::Status;
using ::perception::ThreadPriority;
using ::perception::Write16BitsToPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPort;

namespace {

void SelectDriveOnBus(IdeChannel* channel, bool is_master) {
  uint16 bus = channel->is_primary ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
  Write8BitsToPort(ATA_DRIVE_SELECT(bus), 0xA0 | ((!is_master) << 4));
  // Wait 400ns.
  ATA_SELECT_DELAY(bus);
}

void SelectDriveOnBusIfNotSelected(IdeChannel* channel, bool is_master) {
  if (channel->selected_drive == (int)is_master) return;
  channel->selected_drive = (int)is_master;
  SelectDriveOnBus(channel, is_master);
}

void ProbeChannelDevices(IdeChannel* channel) {
  channel->selected_drive = -1;
  auto buffer = std::make_unique<char[]>(2048);

  for (int j = 0; j < 2; j++) {
    uint8 err = 0;
    uint8 type = IDE_ATA;

    // Select drive
    WriteByteToIdeController(&channel->registers, ATA_REG_HDDEVSEL,
                             0xA0 | (j << 4));
    SleepForDuration(std::chrono::milliseconds(1));

    uint8 initial_status =
        ReadByteFromIdeController(&channel->registers, ATA_REG_STATUS);
    if (initial_status == 0) continue;

    // Send identify command
    WriteByteToIdeController(&channel->registers, ATA_REG_COMMAND,
                             ATA_CMD_IDENTIFY);
    SleepForDuration(std::chrono::milliseconds(1));

    while (true) {
      uint8 status =
          ReadByteFromIdeController(&channel->registers, ATA_REG_STATUS);
      if (status & ATA_SR_ERR) {
        err = 1;
        break;
      }
      if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
    }

    if (err != 0) {
      uint8 cl = ReadByteFromIdeController(&channel->registers, ATA_REG_LBA1);
      uint8 ch = ReadByteFromIdeController(&channel->registers, ATA_REG_LBA2);

      if (cl == 0x14 && ch == 0xEB)
        type = IDE_ATAPI;
      else if (cl == 0x69 && ch == 0x96)
        type = IDE_ATAPI;
      else
        continue;

      WriteByteToIdeController(&channel->registers, ATA_REG_COMMAND,
                               ATA_CMD_IDENTIFY_PACKET);
      SleepForDuration(std::chrono::milliseconds(1));
    }

    ReadBytesFromIdeControllerIntoBuffer(&channel->registers, ATA_REG_DATA,
                                         buffer.get(), 128);

    auto device = std::make_unique<IdeDevice>();
    device->master_drive = j == 0;
    device->type = type;
    device->signature = *(uint16*)&buffer[ATA_IDENT_DEVICETYPE];
    device->capabilities = *(uint16*)&buffer[ATA_IDENT_CAPABILITIES];
    device->command_sets = *(uint32*)&buffer[ATA_IDENT_COMMANDSETS];
    device->size_in_bytes = 0;
    device->channel = channel;

    if (device->command_sets & (1 << 26)) {
      device->size = *(uint32*)&buffer[ATA_IDENT_MAX_LBA_EXT];
    } else {
      device->size = *(uint32*)&buffer[ATA_IDENT_MAX_LBA];
    }

    auto model_chars = std::make_unique<char[]>(41);
    for (int k = 0; k < 40; k += 2) {
      model_chars[k] = buffer[ATA_IDENT_MODEL + k + 1];
      model_chars[k + 1] = buffer[ATA_IDENT_MODEL + k];
    }
    for (int k = 39; k >= 0; k--) {
      if (std::isspace(model_chars[k]))
        model_chars[k] = 0;
      else
        break;
    }
    model_chars[40] = 0;
    device->name = model_chars.get();

    // Probe further
    if (device->type == IDE_ATAPI) {
      // Initialize ATAPI details
      SelectDriveOnBus(channel, device->master_drive);
      uint16 bus = channel->is_primary ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
      Write8BitsToPort(ATA_FEATURES(bus), 0x0);
      Write8BitsToPort(ATA_ADDRESS2(bus), 8);
      Write8BitsToPort(ATA_ADDRESS3(bus), 0);

      Write8BitsToPort(ATA_COMMAND(bus), ATA_DRIVE_MASTER);

      uint8 status;
      while ((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x80)
        SleepForDuration(std::chrono::milliseconds(1));

      while (!((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x8) &&
             !(status & 0x1))
        SleepForDuration(std::chrono::milliseconds(1));

      if (!(status & 0x1)) {
        ResetInterrupt(channel->waiting_on_interrupt,
                       channel->interrupt_triggered);

        uint8 atapi_packet[12] = {
            ATA_CMD_READ_DMA_EXT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (int byte = 0; byte < 12; byte += 2) {
          Write16BitsToPort(ATA_DATA(bus), *(uint16*)&atapi_packet[byte]);
        }

        WaitForInterrupt(channel->waiting_on_interrupt,
                         channel->interrupt_triggered);

        uint32 returnLba = (Read16BitsFromPort(ATA_DATA(bus)) << 0) |
                           (Read16BitsFromPort(ATA_DATA(bus)) << 16);
        uint32 blockLengthInBytes = (Read16BitsFromPort(ATA_DATA(bus)) << 0) |
                                    (Read16BitsFromPort(ATA_DATA(bus)) << 16);

        returnLba = (((returnLba >> 0) & 0xFF) << 24) |
                    (((returnLba >> 8) & 0xFF) << 16) |
                    (((returnLba >> 16) & 0xFF) << 8) |
                    (((returnLba >> 24) & 0xFF) << 0);
        blockLengthInBytes = (((blockLengthInBytes >> 0) & 0xFF) << 24) |
                             (((blockLengthInBytes >> 8) & 0xFF) << 16) |
                             (((blockLengthInBytes >> 16) & 0xFF) << 8) |
                             (((blockLengthInBytes >> 24) & 0xFF) << 0);

        Write8BitsToPort(ATA_FEATURES(bus), 0);  // PIO mode
        Write8BitsToPort(ATA_ADDRESS2(bus), ATAPI_SECTOR_SIZE & 0xFF);
        Write8BitsToPort(ATA_ADDRESS3(bus), ATAPI_SECTOR_SIZE >> 8);

        device->size_in_bytes = returnLba * blockLengthInBytes;
        device->is_writable = false;

        device->storage_device = std::make_unique<IdeStorageDevice>(
            device.get(), channel->supports_dma);
      }
    }

    channel->devices.push_back(std::move(device));
  }
}

Status ExecuteReadOnChannel(IdeChannel* channel, IdeRequest* request) {
  uint16 bus = channel->is_primary ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;

  SelectDriveOnBusIfNotSelected(channel, request->master_drive);

  // Find the corresponding IdeDevice for this request
  IdeDevice* device = nullptr;
  for (auto& dev : channel->devices) {
    if (dev->master_drive == request->master_drive) {
      device = dev.get();
      break;
    }
  }
  if (!device) return Status::INTERNAL_ERROR;

  IdeStorageDevice* storage_device = device->storage_device.get();
  bool use_dma = storage_device && storage_device->SupportsDma();

  size_t start_lba = request->offset_on_device / ATAPI_SECTOR_SIZE;
  size_t end_lba = (request->offset_on_device + request->bytes_to_copy - 1) /
                   ATAPI_SECTOR_SIZE;
  size_t skip_bytes =
      request->offset_on_device - (start_lba * ATAPI_SECTOR_SIZE);
  size_t sectors_to_read = end_lba - start_lba + 1;

  bool can_do_dma = use_dma;
  int scratch_sectors_used = 0;

  struct SectorTransfer {
    bool use_scratch;
    uint8* dest_virtual_address;
    size_t phys_address;
    size_t scratch_offset;
    size_t copy_size;
    size_t skip_bytes;
  };
  std::vector<SectorTransfer> transfers;

  std::vector<void*> allocated_pages;
  auto get_virtual_address = [&](size_t offset) -> uint8* {
    if (request->can_write) {
      return &request->destination_buffer[offset];
    } else {
      size_t start_page = request->offset_in_buffer / kPageSize;
      size_t page_index = offset / kPageSize;
      size_t offset_in_page = offset % kPageSize;
      size_t p = page_index - start_page;
      return (uint8*)allocated_pages[p] + offset_in_page;
    }
  };

  if (can_do_dma) {
    transfers.resize(sectors_to_read);

    // If writing is disabled, allocate pages to assign to the shared memory
    // later
    if (!request->can_write) {
      size_t start_page = request->offset_in_buffer / kPageSize;
      size_t end_page =
          (request->offset_in_buffer + request->bytes_to_copy - 1) / kPageSize;
      size_t num_pages = end_page - start_page + 1;

      allocated_pages.resize(num_pages, nullptr);
      for (size_t p = 0; p < num_pages; p++) {
        size_t page_index = start_page + p;
        size_t page_offset = page_index * kPageSize;

        void* new_page = AllocateMemoryPages(1);
        allocated_pages[p] = new_page;

        bool does_old_memory_exist =
            request->shared_memory->IsPageAllocated(page_offset);
        if (does_old_memory_exist) {
          memcpy(new_page, &request->destination_buffer[page_offset],
                 kPageSize);
        } else {
          memset(new_page, 0, kPageSize);
        }
      }
    }

    size_t current_bytes_to_copy = request->bytes_to_copy;
    size_t current_skip_bytes = skip_bytes;
    size_t current_buffer_offset = request->offset_in_buffer;

    for (size_t i = 0; i < sectors_to_read; i++) {
      size_t copy_size =
          std::min((size_t)ATAPI_SECTOR_SIZE - current_skip_bytes,
                   (size_t)current_bytes_to_copy);
      uint8* dest_addr = get_virtual_address(current_buffer_offset);

      bool needs_scratch = false;
      if (current_skip_bytes > 0 || copy_size < ATAPI_SECTOR_SIZE) {
        needs_scratch = true;
      } else {
        size_t phys_start =
            GetPhysicalAddressOfVirtualAddress((size_t)dest_addr);
        size_t phys_end = GetPhysicalAddressOfVirtualAddress(
            (size_t)(dest_addr + ATAPI_SECTOR_SIZE - 1));
        if (phys_start != 0 && phys_start <= 0xFFFFFFFF &&
            phys_end == phys_start + ATAPI_SECTOR_SIZE - 1 &&
            (phys_start / 65536) == (phys_end / 65536)) {
          transfers[i].phys_address = phys_start;
        } else {
          needs_scratch = true;
        }
      }

      if (needs_scratch) {
        if (scratch_sectors_used >= 2) {
          can_do_dma = false;
          break;
        }
        transfers[i].use_scratch = true;
        transfers[i].scratch_offset =
            kPageSize + (scratch_sectors_used * ATAPI_SECTOR_SIZE);
        transfers[i].phys_address = GetPhysicalAddressOfVirtualAddress(
            (size_t)(storage_device->GetScratchPage() +
                     transfers[i].scratch_offset));
        transfers[i].dest_virtual_address = dest_addr;
        transfers[i].copy_size = copy_size;
        transfers[i].skip_bytes = current_skip_bytes;
        scratch_sectors_used++;
      } else {
        transfers[i].use_scratch = false;
      }

      current_bytes_to_copy -= copy_size;
      current_buffer_offset += copy_size;
      current_skip_bytes = 0;
    }
  }

  if (can_do_dma) {
    // Populate PRDT (Physical Region Descriptor Table)
    uint32* prdt = (uint32*)storage_device->GetScratchPage();
    for (size_t i = 0; i < sectors_to_read; i++) {
      size_t prdt_offset = i * 2;
      prdt[prdt_offset] = (uint32)transfers[i].phys_address;

      uint16 byte_count = ATAPI_SECTOR_SIZE;
      uint16 status = (i == sectors_to_read - 1) ? (1 << 15) : 0;
      prdt[prdt_offset + 1] = byte_count | (status << 16);
    }

    uint16 bus_master_id = channel->registers.bus_master_id;

    // Stop DMA and set direction to read (bit 3 = 0)
    Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), 0);
    // Clear Interrupt and Error status bits
    Write8BitsToPort(ATA_BMR_STATUS(bus_master_id), 6);
    // Set PRDT address
    Write32BitsToPort(ATA_BMR_PRDT(bus_master_id),
                      (size_t)storage_device->GetScratchPagePhysicalAddress());

    // Write Features / Address registers for DMA mode
    Write8BitsToPort(ATA_FEATURES(bus), 1);  // DMA mode
    Write8BitsToPort(ATA_ADDRESS2(bus), 0);
    Write8BitsToPort(ATA_ADDRESS3(bus), 0);

    // Send packet command
    Write8BitsToPort(ATA_COMMAND(bus), ATA_CMD_PACKET);
    ResetInterrupt(channel->waiting_on_interrupt, channel->interrupt_triggered);

    // Poll while busy
    uint8 status = Read8BitsFromPort(ATA_COMMAND(bus));
    while ((status & ATA_SR_BSY) ||
           !(status & (ATA_REG_SECCOUNT1 | ATA_REG_ERROR))) {
      SleepForDuration(std::chrono::milliseconds(10));
      status = Read8BitsFromPort(ATA_COMMAND(bus));
    }

    if (status & ATA_REG_ERROR) {
      if (!request->can_write) {
        for (void* page : allocated_pages) {
          if (page) ReleaseMemoryPages(page, 1);
        }
      }
      return ::perception::Status::MISSING_MEDIA;
    }

    // Send the ATAPI packet
    uint8 atapi_packet[12] = {ATAPI_CMD_READ,
                              0,
                              uint8((start_lba >> 0x18) & 0xFF),
                              uint8((start_lba >> 0x10) & 0xFF),
                              uint8((start_lba >> 0x08) & 0xFF),
                              uint8((start_lba >> 0x00) & 0xFF),
                              uint8((sectors_to_read >> 0x18) & 0xFF),
                              uint8((sectors_to_read >> 0x10) & 0xFF),
                              uint8((sectors_to_read >> 0x08) & 0xFF),
                              uint8((sectors_to_read >> 0x00) & 0xFF),
                              0,
                              0};

    for (int byte = 0; byte < 12; byte += 2)
      Write16BitsToPort(ATA_DATA(bus), *(uint16*)&atapi_packet[byte]);

    // Start Bus Master DMA Engine
    Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), ATA_BMR_COMMAND_START_BIT);

    // Wait for the interrupt signaling end of transfer
    WaitForInterrupt(channel->waiting_on_interrupt,
                     channel->interrupt_triggered);

    // Stop Bus Master DMA
    Write8BitsToPort(ATA_BMR_COMMAND(bus_master_id), 0);

    // Check status
    uint8 bmr_status = Read8BitsFromPort(ATA_BMR_STATUS(bus_master_id));
    if (bmr_status & 2) {  // Error bit set
      if (!request->can_write) {
        for (void* page : allocated_pages) {
          if (page) ReleaseMemoryPages(page, 1);
        }
      }
      std::cout << "IDE DMA Error: BMR Status = " << (int)bmr_status
                << std::endl;
      return Status::INTERNAL_ERROR;
    }

    // Copy data from scratch buffer for any scratch sectors
    for (size_t i = 0; i < sectors_to_read; i++) {
      if (transfers[i].use_scratch) {
        memcpy(transfers[i].dest_virtual_address,
               storage_device->GetScratchPage() + transfers[i].scratch_offset +
                   transfers[i].skip_bytes,
               transfers[i].copy_size);
      }
    }

    // If writing is disabled, assign all the allocated pages to shared memory
    if (!request->can_write) {
      size_t start_page = request->offset_in_buffer / kPageSize;
      size_t end_page =
          (request->offset_in_buffer + request->bytes_to_copy - 1) / kPageSize;
      size_t num_pages = end_page - start_page + 1;
      for (size_t p = 0; p < num_pages; p++) {
        size_t page_index = start_page + p;
        request->shared_memory->AssignPage(allocated_pages[p],
                                           page_index * kPageSize);
      }
    }

    return Status::OK;
  } else {
    // If pages were allocated for DMA and a fallback to PIO occurs, free
    // them first
    if (!request->can_write) {
      for (void* page : allocated_pages) {
        if (page) ReleaseMemoryPages(page, 1);
      }
    }

    // --- PIO FALLBACK ---
    // Explicitly write the Features / Address registers for PIO mode
    Write8BitsToPort(ATA_FEATURES(bus), 0);  // PIO mode
    Write8BitsToPort(ATA_ADDRESS2(bus), ATAPI_SECTOR_SIZE & 0xFF);
    Write8BitsToPort(ATA_ADDRESS3(bus), ATAPI_SECTOR_SIZE >> 8);

    // Send packet command
    Write8BitsToPort(ATA_COMMAND(bus), ATA_CMD_PACKET);
    ResetInterrupt(channel->waiting_on_interrupt, channel->interrupt_triggered);

    // Poll while busy
    uint8 status = Read8BitsFromPort(ATA_COMMAND(bus));
    while ((status & ATA_SR_BSY) ||
           !(status & (ATA_REG_SECCOUNT1 | ATA_REG_ERROR))) {
      SleepForDuration(std::chrono::milliseconds(10));
      status = Read8BitsFromPort(ATA_COMMAND(bus));
    }

    if (status & ATA_REG_ERROR) return ::perception::Status::MISSING_MEDIA;

    // Send the ATAPI packet
    uint8 atapi_packet[12] = {ATAPI_CMD_READ,
                              0,
                              uint8((start_lba >> 0x18) & 0xFF),
                              uint8((start_lba >> 0x10) & 0xFF),
                              uint8((start_lba >> 0x08) & 0xFF),
                              uint8((start_lba >> 0x00) & 0xFF),
                              uint8((sectors_to_read >> 0x18) & 0xFF),
                              uint8((sectors_to_read >> 0x10) & 0xFF),
                              uint8((sectors_to_read >> 0x08) & 0xFF),
                              uint8((sectors_to_read >> 0x00) & 0xFF),
                              0,
                              0};

    for (int byte = 0; byte < 12; byte += 2)
      Write16BitsToPort(ATA_DATA(bus), *(uint16*)&atapi_packet[byte]);

    WaitForInterrupt(channel->waiting_on_interrupt,
                     channel->interrupt_triggered);

    size_t current_page_in_buffer = 0xFFFFFFFFFFFFFFFF;
    bool assign_page = false;
    uint8* current_destination_page = nullptr;

    int64 bytes_to_copy = request->bytes_to_copy;
    int64 buffer_offset = request->offset_in_buffer;

    for (size_t i = 0; i < sectors_to_read; i++) {
      while (1) {
        uint8_t status = Read8BitsFromPort(ATA_COMMAND(bus));
        if (status & ATA_SR_ERR) {
          std::cout << "Device has an error.";
          return ::perception::Status::INTERNAL_ERROR;
        }
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
      }

      int size = Read8BitsFromPort(ATA_ADDRESS3(bus)) << 8 |
                 Read8BitsFromPort(ATA_ADDRESS2(bus));

      for (size_t j = 0; j < size; j += 2) {
        uint16 b = Read16BitsFromPort(ATA_DATA(bus));
        if (bytes_to_copy == 0) {
          continue;
        }

        for (int byte_idx = 0; byte_idx < 2; byte_idx++) {
          if (bytes_to_copy == 0) break;

          if (skip_bytes > 0) {
            skip_bytes--;
            continue;
          }

          uint8 byte_val = (byte_idx == 0) ? (b & 0xFF) : ((b >> 8) & 0xFF);

          size_t buffer_page_index = buffer_offset / kPageSize;
          size_t buffer_page_start = buffer_page_index * kPageSize;
          size_t offset_in_buffer_page = buffer_offset - buffer_page_start;

          if (buffer_page_index != current_page_in_buffer) {
            if (assign_page) {
              request->shared_memory->AssignPage(
                  current_destination_page, current_page_in_buffer * kPageSize);
            }

            current_page_in_buffer = buffer_page_index;
            if (request->can_write) {
              current_destination_page =
                  &request->destination_buffer[buffer_page_start];
              assign_page = false;
            } else {
              current_destination_page = (uint8*)AllocateMemoryPages(1);
              assign_page = true;

              bool does_old_memory_exist =
                  request->shared_memory->IsPageAllocated(buffer_page_start);
              if (does_old_memory_exist) {
                memcpy(current_destination_page,
                       &request->destination_buffer[buffer_page_start],
                       kPageSize);
              } else {
                memset(current_destination_page, 0, kPageSize);
              }
            }
          }

          current_destination_page[offset_in_buffer_page] = byte_val;
          bytes_to_copy--;
          buffer_offset++;
        }
      }
      if (i + 1 < sectors_to_read)
        ResetInterrupt(channel->waiting_on_interrupt,
                       channel->interrupt_triggered);
    }

    if (assign_page) {
      request->shared_memory->AssignPage(current_destination_page,
                                         current_page_in_buffer * kPageSize);
    }

    return Status::OK;
  }
}

}  // namespace

void ChannelWorkerThread(IdeChannel* channel) {
  channel->worker_thread_id = GetThreadId();
  SetThreadPriority(channel->worker_thread_id, ThreadPriority::Background);

  while (true) {
    IdeRequest* request = channel->queue.Pop();
    if (request == nullptr) {
      channel->worker_thread_sleeping.store(true, std::memory_order_release);
      if (channel->queue.Peek() == nullptr) {
        SleepThisThread();
      }
      channel->worker_thread_sleeping.store(false, std::memory_order_release);
      continue;
    }

    if (request->type == IdeRequestType::INITIALIZE) {
      ProbeChannelDevices(channel);
      request->completed.store(true, std::memory_order_release);
      request->fiber_to_wake->WakeUp();
    } else if (request->type == IdeRequestType::READ) {
      request->status = ExecuteReadOnChannel(channel, request);
      request->completed.store(true, std::memory_order_release);
      request->fiber_to_wake->WakeUp();
    }
  }
}
