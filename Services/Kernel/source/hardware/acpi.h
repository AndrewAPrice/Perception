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

#include "hardware/registers.h"
#include "types.h"

namespace hardware {

// Discovers and parses ACPI tables during early kernel boot.
void InitializeAcpi();

// Performs ACPI transition to sleep state S5 (soft off).
void AcpiPowerOff();

// Performs ACPI hardware system reset if supported by FADT.
void AcpiReset();

// Returns whether ACPI S5 sleep state configuration was successfully discovered.
bool HasAcpiS5();

// Returns whether ACPI hardware reset configuration is available.
bool HasAcpiReset();

// Populates user registers with ACPI power and table details for Syscall 73.
void PopulateRegistersWithAcpiDetails(Registers& regs);

// Returns the number of CPU cores discovered via the ACPI MADT table.
size_t GetDiscoveredCoreCount();

// Returns the APIC ID of the core at the given index.
uint32 GetCoreApicId(size_t core_index);

// Returns the physical address of the Local APIC.
size_t GetLocalApicPhysicalAddress();

// Returns the physical address of the I/O APIC.
size_t GetIoApicPhysicalAddress();

// Parses MADT table data and populates core APIC IDs.
// Exposed for unit testing.
bool ParseMadtTable(const void* table_data, size_t length,
                    size_t& lapic_address, size_t& core_count,
                    uint32* core_apic_ids, size_t max_cores,
                    size_t& io_apic_address);

// Parses an ACPI AML package to extract S5 sleep type integers SLP_TYPa and SLP_TYPb.
// Exposed for unit testing.
bool ParseAmlS5Package(const uint8* data, size_t length, uint8& slp_typa, uint8& slp_typb);

// Validates an ACPI table checksum.
// Exposed for unit testing.
bool ValidateAcpiTableChecksum(const void* table, size_t length);

}  // namespace hardware



