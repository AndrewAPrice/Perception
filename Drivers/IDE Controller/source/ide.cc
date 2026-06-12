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

#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "ata.h"
#include "ide_types.h"
#include "ide_worker.h"
#include "interrupts.h"
#include "io.h"
#include "perception/devices/device_manager.h"
#include "perception/fibers.h"
#include "perception/pci.h"
#include "perception/services.h"
#include "perception/threads.h"

using ::perception::Fiber;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::GetService;
using ::perception::kPciHdrBar0;
using ::perception::kPciHdrBar1;
using ::perception::kPciHdrBar2;
using ::perception::kPciHdrBar3;
using ::perception::kPciHdrBar4;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Sleep;
using ::perception::WakeThread;
using ::perception::Write8BitsToPciConfig;
using ::perception::devices::DeviceManager;
using ::perception::devices::PciDevice;
using ::perception::devices::PciDeviceFilter;
using ::perception::devices::PciDeviceFilters;

namespace {

std::vector<std::unique_ptr<IdeController>> ide_controllers;

void InitializeIdeController(uint8 bus, uint8 slot, uint8 function,
                             uint8 prog_if) {
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

  bool supports_dma = (prog_if & (1 << 7)) != 0;

  controller->channels[ATA_PRIMARY].registers.io_base =
      (bar0 & 0xFFFC) + 0x1F0 * (!bar0);
  controller->channels[ATA_PRIMARY].registers.control_base =
      (bar1 & 0xFFFC) + 0x3F4 * (!bar1);
  controller->channels[ATA_SECONDARY].registers.io_base =
      (bar2 & 0xFFFC) + 0x170 * (!bar2);
  controller->channels[ATA_SECONDARY].registers.control_base =
      (bar3 & 0xFFFC) + 0x374 * (!bar3);
  size_t bus_master_id = bar4;
  if (bus_master_id & 1) bus_master_id &= 0xFFFFFFFC;
  controller->channels[ATA_PRIMARY].registers.bus_master_id = bus_master_id;
  controller->channels[ATA_SECONDARY].registers.bus_master_id =
      bus_master_id + 8;

  // Enable interrupts for probing.
  WriteByteToIdeController(&controller->channels[ATA_PRIMARY].registers,
                           ATA_REG_CONTROL, 0);
  WriteByteToIdeController(&controller->channels[ATA_SECONDARY].registers,
                           ATA_REG_CONTROL, 0);

  // Initialize and spawn worker threads.
  for (int i = 0; i < 2; ++i) {
    controller->channels[i].is_primary = (i == 0);
    controller->channels[i].supports_dma = supports_dma;
    controller->channels[i].selected_drive = -1;
    controller->channels[i].waiting_on_interrupt = nullptr;
    controller->channels[i].interrupt_triggered = true;
    controller->channels[i].worker_thread_sleeping.store(
        false, std::memory_order_seq_cst);
    std::thread worker(ChannelWorkerThread, &controller->channels[i]);
    worker.detach();
  }

  static bool interrupts_initialized = false;
  if (!interrupts_initialized) {
    InitializeInterrupts(
        controller->channels[ATA_PRIMARY].waiting_on_interrupt,
        controller->channels[ATA_PRIMARY].interrupt_triggered,
        controller->channels[ATA_SECONDARY].waiting_on_interrupt,
        controller->channels[ATA_SECONDARY].interrupt_triggered);
    interrupts_initialized = true;
  }

  // Send initialization request to both channels.
  for (int i = 0; i < 2; ++i) {
    IdeRequest req;
    req.type = IdeRequestType::INITIALIZE;
    req.completed.store(false, std::memory_order_release);
    req.fiber_to_wake = GetCurrentlyExecutingFiber();

    controller->channels[i].queue.Push(&req);
    if (controller->channels[i].worker_thread_sleeping.load(
            std::memory_order_seq_cst)) {
      WakeThread(controller->channels[i].worker_thread_id);
    }

    while (!req.completed.load(std::memory_order_acquire)) {
      Sleep();
    }
  }

  // Re-enable interrupts on channels that have devices, and disable on those
  // that don't.
  for (int i = 0; i < 2; ++i) {
    bool has_devices = !controller->channels[i].devices.empty();
    WriteByteToIdeController(&controller->channels[i].registers,
                             ATA_REG_CONTROL, has_devices ? 0 : 2);
  }

  ide_controllers.push_back(std::move(controller));
}

}  // namespace

void InitializeIdeControllers() {
  PciDeviceFilters filters;

  PciDeviceFilter base_class_filter;
  base_class_filter.key = PciDeviceFilter::Key::BASE_CLASS;
  base_class_filter.value = 0x01;
  filters.filters.push_back(base_class_filter);

  PciDeviceFilter sub_class_filter;
  sub_class_filter.key = PciDeviceFilter::Key::SUB_CLASS;
  sub_class_filter.value = 0x01;
  filters.filters.push_back(sub_class_filter);

  auto status_or_devices = GetService<DeviceManager>().QueryPciDevices(filters);
  if (!status_or_devices) return;

  for (const auto& device : status_or_devices->devices) {
    std::cout << "Initializing " << device.name << std::endl;

    InitializeIdeController(device.bus, device.slot, device.function,
                            device.prog_if);
  }
}
