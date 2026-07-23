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

#include "perception/devices/device_manager.h"
#include "perception/pci.h"
#include "types.h"

// Virtio Status Register Bits
constexpr uint8 kVirtioStatusAcknowledge = 1;
constexpr uint8 kVirtioStatusDriver = 2;
constexpr uint8 kVirtioStatusDriverOk = 4;
constexpr uint8 kVirtioStatusFeaturesOk = 8;

// Virtio Legacy PCI Port Offsets
constexpr uint16 kVirtioPciHostFeatures = 0;
constexpr uint16 kVirtioPciGuestFeatures = 4;
constexpr uint16 kVirtioPciQueuePfn = 8;
constexpr uint16 kVirtioPciQueueNum = 12;
constexpr uint16 kVirtioPciQueueSel = 14;
constexpr uint16 kVirtioPciQueueNotify = 16;
constexpr uint16 kVirtioPciStatus = 18;
constexpr uint16 kVirtioPciIsr = 19;

// Enables Bus Master, IO Space, and Memory Space, and clear Interrupt Disable
// in PCI Command register.
void EnableVirtioPciDevice(const perception::devices::PciDevice& device);

// Reads IRQ line from offset 0x3C in PCI config space.
uint8 GetPciInterruptLine(const perception::devices::PciDevice& device);

// Performs VirtIO Legacy Reset & ACK/DRIVER handshake and clear modern feature
// requests
void ResetLegacyVirtioDevice(uint16 io_base);

// Sets DRIVER_OK status bit on legacy device.
void SetVirtioDriverOk(uint16 io_base);
