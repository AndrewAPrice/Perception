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

#include "ide.h"

#include <cctype>
#include <iostream>
#include <memory>
#include <vector>

#include "ata.h"
#include "ide_types.h"
#include "interrupts.h"
#include "io.h"
#include "perception/devices/device_manager.h"
#include "perception/pci.h"
#include "perception/port_io.h"
#include "perception/services.h"
#include "perception/threads.h"
#include "perception/time.h"

using ::perception::GetService;
using ::perception::kPciHdrBar0;
using ::perception::kPciHdrBar1;
using ::perception::kPciHdrBar2;
using ::perception::kPciHdrBar3;
using ::perception::kPciHdrBar4;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Read8BitsFromPort;
using ::perception::SleepForDuration;
using ::perception::Write16BitsToPort;
using ::perception::Write8BitsToPciConfig;
using ::perception::Write8BitsToPort;
using ::perception::Yield;
using ::perception::devices::DeviceManager;
using ::perception::devices::PciDevice;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;

namespace {

std::vector<std::unique_ptr<IdeController>> ide_controllers;
std::mutex ide_mutex;

// The currently selected drive on the primary bus. -1 means no drive is
// selected.
int primary_bus_drive;

// The currently selected drive on the secondary bus. -1 means no drive is
// selected.
int secondary_bus_drive;

void SelectDriveOnBus(bool is_primary_channel, bool is_primary_drive) {
  uint16 bus = is_primary_channel ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
  Write8BitsToPort(ATA_DRIVE_SELECT(bus), (!is_primary_drive) << 4);
  // Wait 400ns.
  ATA_SELECT_DELAY(bus);
}

void MaybeInitializeIdeDevice(IdeDevice *device, bool supports_dma) {
  if (device->type != IDE_ATAPI) {
    // We currently only support CD drives.
    std::cout << "Not sure what to do with " << device->name << std::endl;
    return;
  }

  // Select the drive.
  SelectDriveOnBus(device->primary_channel, device->master_drive);

  // Set features register to 0 (PIO Mode).
  uint16 bus = device->primary_channel ? ATA_BUS_PRIMARY : ATA_BUS_SECONDARY;
  Write8BitsToPort(ATA_FEATURES(bus), 0x0);

  // Set lba1 and lba2 registers to 0x0008 (number of bytes will be returned )
  Write8BitsToPort(ATA_ADDRESS2(bus), 8);  // ATAPI_SECTOR_SIZE & 0xFF);
  Write8BitsToPort(ATA_ADDRESS3(bus), 0);  // ATAPI_SECTOR_SIZE >> 8);

  // send packet command
  Write8BitsToPort(ATA_COMMAND(bus), ATA_DRIVE_MASTER);

  // Poll.
  uint8 status;
  while ((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x80) /* busy */
    SleepForDuration(std::chrono::milliseconds(1));

  while (!((status = Read8BitsFromPort(ATA_COMMAND(bus))) & 0x8) &&
         !(status & 0x1))
    SleepForDuration(std::chrono::milliseconds(1));

  if (status & 0x1) {
    // There is an error - likely no disk.
    return;
  }

  ResetInterrupt(device->primary_channel);

  // Send the atapi packet - must be 6 words (12 bytes) long.
  uint8 atapi_packet[12] = {
      ATA_CMD_READ_DMA_EXT, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (int byte = 0; byte < 12; byte += 2) {
    Write16BitsToPort(ATA_DATA(bus), *(uint16 *)&atapi_packet[byte]);
  }

  WaitForInterrupt(device->primary_channel);

  // Read 4 words (8 bytes) from the data register.
  uint32 returnLba = (Read16BitsFromPort(ATA_DATA(bus)) << 0) |
                     (Read16BitsFromPort(ATA_DATA(bus)) << 16);

  uint32 blockLengthInBytes = (Read16BitsFromPort(ATA_DATA(bus)) << 0) |
                              (Read16BitsFromPort(ATA_DATA(bus)) << 16);

  // Flip the endiness.
  returnLba =
      (((returnLba >> 0) & 0xFF) << 24) | (((returnLba >> 8) & 0xFF) << 16) |
      (((returnLba >> 16) & 0xFF) << 8) | (((returnLba >> 24) & 0xFF) << 0);

  blockLengthInBytes = (((blockLengthInBytes >> 0) & 0xFF) << 24) |
                       (((blockLengthInBytes >> 8) & 0xFF) << 16) |
                       (((blockLengthInBytes >> 16) & 0xFF) << 8) |
                       (((blockLengthInBytes >> 24) & 0xFF) << 0);

  // Prepare the device for DMA or not.
  // Set features register to 0 (PIO Mode).
  Write8BitsToPort(ATA_FEATURES(bus), supports_dma ? 1 : 0);

  // Set lba1 and lba2 registers to 0x0008 (number of bytes will be returned).
  Write8BitsToPort(ATA_ADDRESS2(bus),
                   supports_dma ? 0 : (ATAPI_SECTOR_SIZE & 0xFF));
  Write8BitsToPort(ATA_ADDRESS3(bus),
                   supports_dma ? 0 : (ATAPI_SECTOR_SIZE >> 8));

  // Calculate the device size
  device->size_in_bytes = returnLba * blockLengthInBytes;
  device->is_writable = false;

  device->storage_device =
      std::make_unique<IdeStorageDevice>(device, supports_dma);
}

void MaybeInitializeIdeDevices(IdeController *controller, bool supports_dma) {
  primary_bus_drive = -1;
  secondary_bus_drive = -1;

  auto buffer = std::make_unique<char[]>(2048);
  // Detect ATA-ATAPI devices.
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < 2; j++) {
      uint8 err = 0;
      uint8 type = IDE_ATA;

      // Select drive.
      WriteByteToIdeController(&controller->channels[i], ATA_REG_HDDEVSEL,
                               0xA0 | (j << 4));

      SleepForDuration(std::chrono::milliseconds(1));

      // Send the identify command.
      WriteByteToIdeController(&controller->channels[i], ATA_REG_COMMAND,
                               ATA_CMD_IDENTIFY);

      SleepForDuration(std::chrono::milliseconds(1));

      if (ReadByteFromIdeController(&controller->channels[i], ATA_REG_STATUS) ==
          0) {
        // No device.
        continue;
      }

      while (true) {
        uint8 status =
            ReadByteFromIdeController(&controller->channels[i], ATA_REG_STATUS);
        if ((status & ATA_SR_ERR)) {
          err = 1;
          break;
        }  // Not ATA.
        if (!(status & ATA_SR_BSY) && (status & ATA_SR_DRQ)) break;
      }

      // Probe for ATAPI device.
      if (err != 0) {
        uint8 cl =
            ReadByteFromIdeController(&controller->channels[i], ATA_REG_LBA1);
        uint8 ch =
            ReadByteFromIdeController(&controller->channels[i], ATA_REG_LBA2);

        if (cl == 0x14 && ch == 0xEB)
          type = IDE_ATAPI;
        else if (cl == 0x69 && ch == 0x96)
          type = IDE_ATAPI;
        else {
          // Unknown disk type.
          continue;
        }

        WriteByteToIdeController(&controller->channels[i], ATA_REG_COMMAND,
                                 ATA_CMD_IDENTIFY_PACKET);
        SleepForDuration(std::chrono::milliseconds(1));
      }

      ReadBytesFromIdeControllerIntoBuffer(&controller->channels[i],
                                           ATA_REG_DATA, buffer.get(), 128);
      auto device = std::make_unique<IdeDevice>();
      device->type = type;
      device->primary_channel = i == 0;
      device->master_drive = j == 0;
      device->signature = *(uint16 *)&buffer[ATA_IDENT_DEVICETYPE];
      device->capabilities = *(uint16 *)&buffer[ATA_IDENT_CAPABILITIES];
      device->command_sets = *(uint32 *)&buffer[ATA_IDENT_COMMANDSETS];
      device->size_in_bytes = 0;
      device->controller = controller;

      // Read the size.
      if (device->command_sets & (1 << 26)) {
        // 48-bit addressing
        device->size = *(uint32 *)&buffer[ATA_IDENT_MAX_LBA_EXT];
      } else {
        // 28-bit addressing or CHS
        device->size = *(uint32 *)&buffer[ATA_IDENT_MAX_LBA];
      }

      auto model_chars = std::make_unique<char[]>(41);

      // Copy out the device model name.
      for (int k = 0; k < 40; k += 2) {
        model_chars[k] = buffer[ATA_IDENT_MODEL + k + 1];
        model_chars[k + 1] = buffer[ATA_IDENT_MODEL + k];
      }
      // Trim whitespace off the end.
      for (int k = 39; k >= 0; k--) {
        if (std::isspace(model_chars[k])) {
          model_chars[k] = 0;
        } else {
          break;
        }
      }
      model_chars[40] = 0;
      device->name = model_chars.get();
      MaybeInitializeIdeDevice(device.get(), supports_dma);

      controller->devices.push_back(std::move(device));
    }
  }
}

void InitializeIdeController(uint8 bus, uint8 slot, uint8 function,
                             uint8 prog_if) {
  std::lock_guard<std::mutex> mutex(GetIdeMutex());

  auto controller = std::make_unique<IdeController>();

  // Read in the ports.
  uint16 bar0 = Read16BitsFromPciConfig(bus, slot, function, kPciHdrBar0);
  uint16 bar1 = Read16BitsFromPciConfig(bus, slot, function, kPciHdrBar1);
  uint16 bar2 = Read16BitsFromPciConfig(bus, slot, function, kPciHdrBar2);
  uint16 bar3 = Read16BitsFromPciConfig(bus, slot, function, kPciHdrBar3);
  uint16 bar4 = Read16BitsFromPciConfig(bus, slot, function, kPciHdrBar4);

  // Turn on the bus master command in the PCI config.
  uint8 command = Read8BitsFromPciConfig(bus, slot, function, kPciHdrCommand);
  command |= kPciHdrCommandBitBusMaster;
  Write8BitsToPciConfig(bus, slot, function, kPciHdrCommand, command);

  controller->channels[ATA_PRIMARY].io_base = (bar0 & 0xFFFC) + 0x1F0 * (!bar0);
  controller->channels[ATA_PRIMARY].control_base =
      (bar1 & 0xFFFC) + 0x3F6 * (!bar1);
  controller->channels[ATA_SECONDARY].io_base =
      (bar2 & 0xFFFC) + 0x170 * (!bar2);
  controller->channels[ATA_SECONDARY].control_base =
      (bar3 & 0xFFFC) + 0x736 * (!bar3);
  size_t bus_master_id = bar4;
  if (bus_master_id & 1) bus_master_id &= 0xFFFFFFFC;
  controller->channels[ATA_PRIMARY].bus_master_id = bus_master_id;
  controller->channels[ATA_SECONDARY].bus_master_id = bus_master_id + 8;

  // Temporarily disabled until the reading code is reimplemented.
  bool supports_dma = false;  // prog_if & (1 << 7);

#if 0
	if(prog_if == 0x8A || prog_if == 0x80) {
		// IDE - IRQ 14 and 15
	} else {
		// SATA
	}
#endif

  // Disable interrupts.
  WriteByteToIdeController(&controller->channels[ATA_PRIMARY], ATA_REG_CONTROL,
                           2);
  WriteByteToIdeController(&controller->channels[ATA_SECONDARY],
                           ATA_REG_CONTROL, 2);

  MaybeInitializeIdeDevices(controller.get(), supports_dma);

  ide_controllers.push_back(std::move(controller));
}

}  // namespace

void InitializeIdeControllers() {
  PciDeviceFilters filters;

  {
    auto &base_class_filter = filters.filters.emplace_back();
    base_class_filter.key = PciDeviceFilter::Key::BASE_CLASS;
    base_class_filter.value = 0x01;
  }

  {
    auto &base_class_filter = filters.filters.emplace_back();
    base_class_filter.key = PciDeviceFilter::Key::SUB_CLASS;
    base_class_filter.value = 0x01;
  }

  auto status_or_devices = GetService<DeviceManager>().QueryPciDevices(filters);
  if (!status_or_devices) return;

  for (const auto &device : status_or_devices->devices) {
    std::cout << "Initializing " << device.name << std::endl;

    InitializeIdeController(device.bus, device.slot, device.function,
                            device.prog_if);
  }
}

std::mutex &GetIdeMutex() { return ide_mutex; }

void SelectDriveOnBusIfNotSelected(bool is_primary_channel,
                                   bool is_primary_drive) {
  uint16 bus;

  if (is_primary_channel) {
    if (primary_bus_drive == (int)is_primary_drive) return;
    primary_bus_drive = (int)is_primary_drive;
    bus = ATA_BUS_PRIMARY;
  } else {
    if (secondary_bus_drive == (int)is_primary_drive) return;
    secondary_bus_drive = (int)is_primary_drive;
    bus = ATA_BUS_SECONDARY;
  }

  SelectDriveOnBus(is_primary_channel, is_primary_drive);
}
