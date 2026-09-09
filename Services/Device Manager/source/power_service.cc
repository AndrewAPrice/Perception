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

#include "perception/acpi.h"
#include "perception/permissions.h"
#include "perception/port_io.h"
#include "perception/services.h"

using ::perception::DoesProcessHavePermission;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::NotifyWhenServiceDisappears;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPort;
using ::perception::Read8BitsFromPort;
using ::perception::Write16BitsToPort;
using ::perception::Write8BitsToPort;
using ::perception::devices::PowerListener;

namespace {

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

// Bit in PM1 control register indicating ACPI mode is enabled.
constexpr uint16 kSciEnableBit = 0x0001;

// Bit in PM1 control register triggering sleep transition.
constexpr uint16 kSleepEnableBit = 0x2000;

// Maximum iterations to wait for ACPI mode enable transition.
constexpr int kAcpiEnableTimeoutIterations = 300;

// Standard PC diagnostic I/O delay port.
constexpr uint16 kIoDelayPort = 0x80;

// ACPI configuration details queried from kernel.
::perception::AcpiDetails acpi_details;

// Whether ACPI details have been queried from the kernel.
bool has_queried_acpi = false;

// Queries ACPI details from the kernel on first use.
void EnsureAcpiQueried() {
  if (has_queried_acpi) return;
  has_queried_acpi = true;
  (void)::perception::GetAcpiDetails(acpi_details);
}

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

  EnsureAcpiQueried();
  if (acpi_details.has_s5 && acpi_details.pm1a_control_port != 0) {
    if ((Read16BitsFromPort(acpi_details.pm1a_control_port) & kSciEnableBit) ==
            0 &&
        acpi_details.smi_cmd_port != 0 && acpi_details.acpi_enable_value != 0) {
      Write8BitsToPort(acpi_details.smi_cmd_port,
                       acpi_details.acpi_enable_value);
      for (int i = 0; i < kAcpiEnableTimeoutIterations; i++) {
        if ((Read16BitsFromPort(acpi_details.pm1a_control_port) &
             kSciEnableBit) != 0)
          break;
        (void)Read8BitsFromPort(kIoDelayPort);
      }
    }

    uint16 val_a =
        static_cast<uint16>((acpi_details.slp_typa << 10) | kSleepEnableBit);
    Write16BitsToPort(acpi_details.pm1a_control_port, val_a);

    if (acpi_details.pm1b_control_port != 0) {
      uint16 val_b =
          static_cast<uint16>((acpi_details.slp_typb << 10) | kSleepEnableBit);
      Write16BitsToPort(acpi_details.pm1b_control_port, val_b);
    }
  }

  return Status::OK;
}

Status PowerService::Restart(ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanPowerDown))
    return Status::NOT_ALLOWED;

  EnsureAcpiQueried();
  if (acpi_details.has_reset && acpi_details.reset_port != 0)
    Write8BitsToPort(acpi_details.reset_port, acpi_details.reset_value);

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
