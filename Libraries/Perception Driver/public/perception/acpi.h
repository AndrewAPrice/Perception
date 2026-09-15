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

#include "types.h"

namespace perception {

// ACPI details populated by the kernel.
struct AcpiDetails {
  uint16 pm1a_control_port;
  uint16 pm1b_control_port;
  uint8 slp_typa;
  uint8 slp_typb;
  bool has_s5;
  uint16 smi_cmd_port;
  uint8 acpi_enable_value;
  uint16 reset_port;
  uint8 reset_value;
  bool has_reset;
  size_t rsdp_physical_address;
};

// Queries ACPI power management, reset, and table details from the kernel.
// Only drivers may call this. Returns true if ACPI information was retrieved.
bool GetAcpiDetails(AcpiDetails& details);

}  // namespace perception
