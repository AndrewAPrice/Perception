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

#include "virtio.h"

#include "perception/pci.h"
#include "perception/port_io.h"
#include "types.h"

using ::perception::Read32BitsFromPort;
using ::perception::Read8BitsFromPciConfig;
using ::perception::Read8BitsFromPort;
using ::perception::Write32BitsToPort;
using ::perception::Write8BitsToPciConfig;
using ::perception::Write8BitsToPort;
using ::perception::devices::PciDevice;

namespace {

constexpr uint16 kPciCommandInterruptDisableBit = 1 << 10;
constexpr uint8 kPciConfigInterruptLineOffset = 0x3C;
constexpr uint8 kByteMask = 0xFF;
constexpr int kBitsPerByte = 8;
constexpr uint8 kVirtioStatusReset = 0;
constexpr uint32 kDefaultGuestFeatures = 0;

}  // namespace

void EnableVirtioPciDevice(const PciDevice& device) {
  uint8 cmd_low = Read8BitsFromPciConfig(
      device.bus, device.slot, device.function, perception::kPciHdrCommand);
  uint8 cmd_high = Read8BitsFromPciConfig(
      device.bus, device.slot, device.function, perception::kPciHdrCommand + 1);
  uint16 command = cmd_low | (cmd_high << kBitsPerByte);
  command |= perception::kPciHdrCommandBitIoSpace |
             perception::kPciHdrCommandBitMemorySpace |
             perception::kPciHdrCommandBitBusMaster;
  command &=
      ~kPciCommandInterruptDisableBit;  // Clear Interrupt Disable bit (bit 10)
  Write8BitsToPciConfig(device.bus, device.slot, device.function,
                        perception::kPciHdrCommand, command & kByteMask);
  Write8BitsToPciConfig(device.bus, device.slot, device.function,
                        perception::kPciHdrCommand + 1,
                        (command >> kBitsPerByte) & kByteMask);
}

uint8 GetPciInterruptLine(const PciDevice& device) {
  return Read8BitsFromPciConfig(device.bus, device.slot, device.function,
                                kPciConfigInterruptLineOffset);
}

void ResetLegacyVirtioDevice(uint16 io_base) {
  Write8BitsToPort(io_base + kVirtioPciStatus, kVirtioStatusReset);  // Reset
  Write8BitsToPort(io_base + kVirtioPciStatus,
                   Read8BitsFromPort(io_base + kVirtioPciStatus) |
                       kVirtioStatusAcknowledge);  // ACKNOWLEDGE
  Write8BitsToPort(io_base + kVirtioPciStatus,
                   Read8BitsFromPort(io_base + kVirtioPciStatus) |
                       kVirtioStatusDriver);  // DRIVER

  // Negotiate features by reading host features and writing 0 to guest features
  (void)Read32BitsFromPort(io_base + kVirtioPciHostFeatures);
  Write32BitsToPort(io_base + kVirtioPciGuestFeatures, kDefaultGuestFeatures);
}

void SetVirtioDriverOk(uint16 io_base) {
  Write8BitsToPort(io_base + kVirtioPciStatus,
                   Read8BitsFromPort(io_base + kVirtioPciStatus) |
                       kVirtioStatusDriverOk);  // DRIVER_OK
}
