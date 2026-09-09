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

#include "power_service.h"

#include "perception/permissions.h"
#include "perception/port_io.h"
#include "perception/services.h"

using ::perception::DoesProcessHavePermission;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::NotifyWhenServiceDisappears;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read8BitsFromPort;
using ::perception::Write16BitsToPort;
using ::perception::Write8BitsToPort;
using ::perception::devices::PowerListener;

namespace {

// QEMU default (PIIX4) ACPI PM1a_CNT I/O port.
constexpr uint16 kQemuAcpiPm1ControlPort = 0x604;

// ACPI sleep enable bit and sleep type S5 value for QEMU PIIX4.
constexpr uint16 kAcpiS5SleepCommandPiix4 = 0x2000;

// ACPI sleep enable bit and sleep type S5 value for QEMU Q35.
constexpr uint16 kAcpiS5SleepCommandQ35 = 0x3400;

// Bochs and older QEMU poweroff I/O port.
constexpr uint16 kBochsPowerControlPort = 0xB004;

// Bochs and older QEMU poweroff command.
constexpr uint16 kBochsPowerOffCommand = 0x2000;

// VirtualBox poweroff I/O port.
constexpr uint16 kVirtualBoxPowerControlPort = 0x4004;

// VirtualBox poweroff command.
constexpr uint16 kVirtualBoxPowerOffCommand = 0x3400;

// QEMU debug exit I/O port configured in run_qemu.sh.
constexpr uint16 kQemuDebugExitPort = 0xF4;

// Fast reset / PCI reset control register I/O port.
constexpr uint16 kPciResetControlPort = 0xCF9;

// Value written to prepare PCI reset.
constexpr uint8 kPciResetPrepare = 0x02;

// Value written to trigger PCI reset.
constexpr uint8 kPciResetTrigger = 0x06;

// PS/2 keyboard controller status and command I/O port.
constexpr uint16 kPs2CommandPort = 0x64;

// PS/2 keyboard controller pulse reset command.
constexpr uint8 kPs2PulseResetCommand = 0xFE;

// Bit mask indicating PS/2 input buffer is full.
constexpr uint8 kPs2InputBufferFull = 0x02;

// Maximum iterations to wait for PS/2 input buffer to empty.
constexpr int kPs2BufferTimeoutIterations = 10000;

}  // namespace

PowerService::PowerService() {
  NotifyOnEachNewServiceInstance<PowerListener>(
      [this](PowerListener::Client power_listener) {
        power_listeners_.insert(power_listener);
        NotifyWhenServiceDisappears(power_listener, [this, power_listener]() {
          power_listeners_.erase(power_listener);
        });
      });
}

Status PowerService::PowerOff(ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanPowerDown))
    return Status::NOT_ALLOWED;

  Write16BitsToPort(kQemuAcpiPm1ControlPort, kAcpiS5SleepCommandPiix4);
  Write16BitsToPort(kQemuAcpiPm1ControlPort, kAcpiS5SleepCommandQ35);
  Write16BitsToPort(kBochsPowerControlPort, kBochsPowerOffCommand);
  Write16BitsToPort(kVirtualBoxPowerControlPort, kVirtualBoxPowerOffCommand);
  Write8BitsToPort(kQemuDebugExitPort, 0x00);
  return Status::OK;
}

Status PowerService::Restart(ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanPowerDown))
    return Status::NOT_ALLOWED;

  Write8BitsToPort(kPciResetControlPort, kPciResetPrepare);
  Write8BitsToPort(kPciResetControlPort, kPciResetTrigger);

  for (int i = 0; i < kPs2BufferTimeoutIterations; i++) {
    if ((Read8BitsFromPort(kPs2CommandPort) & kPs2InputBufferFull) == 0) break;
  }
  Write8BitsToPort(kPs2CommandPort, kPs2PulseResetCommand);
  return Status::OK;
}

Status PowerService::Sleep(ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanPowerDown))
    return Status::NOT_ALLOWED;

  for (auto listener : power_listeners_) (void)listener.OnSleep();
  return Status::OK;
}

Status PowerService::Wake(ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanWake))
    return Status::NOT_ALLOWED;

  for (auto listener : power_listeners_) (void)listener.OnWake();
  return Status::OK;
}
