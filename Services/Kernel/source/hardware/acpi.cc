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

#include "hardware/acpi.h"

#include "common/kernel_string.h"
#include "hardware/current_core.h"

#ifndef TEST
#include "../../../third_party/multiboot2.h"
#include "hardware/io.h"
#include "memory/physical_allocator.h"
#include "memory/virtual_address_space.h"
#include "memory/virtual_allocator.h"
#include "output/text_terminal.h"
#endif

namespace hardware {

#ifndef TEST
using common::MemoryEquals;
using memory::KernelAddressSpace;
using memory::kPageSize;
using memory::kVirtualMemoryOffset;
using output::NumberFormat;
using output::print;
#endif

namespace {

// Base physical address of the 16-bit EBDA segment pointer.
constexpr size_t kEbdaSegmentPointerPhys = 0x40E;

// Base physical address of the BIOS ROM area.
constexpr size_t kBiosRomStartPhys = 0xE0000;

// Length in bytes of the BIOS ROM area to scan.
constexpr size_t kBiosRomLength = 0x20000;

// Alignment boundary for RSDP descriptor structures in memory.
constexpr size_t kRsdpAlign = 16;

// Bit in PM1 control register indicating ACPI mode is enabled.
constexpr uint16 kSciEnableBit = 0x0001;

// Bit in PM1 control register triggering sleep transition.
constexpr uint16 kSleepEnableBit = 0x2000;

// Maximum iterations to wait for ACPI mode enable transition.
constexpr int kAcpiEnableTimeoutIterations = 300;

// Flag bit in FADT indicating reset register support.
constexpr uint32 kResetRegSupportedFlag = 1 << 10;

// Address space ID indicating System I/O ports.
constexpr uint8 kSystemIoAddressSpaceId = 1;

// Encoded AML integer types and prefixes.
enum class AmlIntegerType : uint8 {
  // Constant zero integer.
  Zero = 0x00,

  // Constant one integer.
  One = 0x01,

  // Byte-sized integer followed by 8-bit data.
  Byte = 0x0A,

  // Word-sized integer followed by 16-bit little-endian data.
  Word = 0x0B,

  // DWord-sized integer followed by 32-bit little-endian data.
  DWord = 0x0C,

  // Constant integer of all ones (0xFF).
  Ones = 0xFF
};

// ACPI 1.0 Root System Description Pointer descriptor.
struct AcpiRsdpDescriptor {
  char signature[8];
  uint8 checksum;
  char oem_id[6];
  uint8 revision;
  uint32 rsdt_address;
} __attribute__((packed));

// ACPI 2.0+ Root System Description Pointer descriptor.
struct AcpiRsdpExtendedDescriptor {
  AcpiRsdpDescriptor first_part;
  uint32 length;
  uint64 xsdt_address;
  uint8 extended_checksum;
  uint8 reserved[3];
} __attribute__((packed));

// Standard ACPI System Description Table header.
struct AcpiTableHeader {
  char signature[4];
  uint32 length;
  uint8 revision;
  uint8 checksum;
  char oem_id[6];
  char oem_table_id[8];
  uint32 oem_revision;
  uint32 creator_id;
  uint32 creator_revision;
} __attribute__((packed));

// ACPI Generic Address Structure.
struct AcpiGas {
  uint8 address_space_id;
  uint8 register_bit_width;
  uint8 register_bit_offset;
  uint8 access_size;
  uint64 address;
} __attribute__((packed));

// Fixed ACPI Description Table (FADT).
struct AcpiFadt {
  AcpiTableHeader header;
  uint32 firmware_ctrl;
  uint32 dsdt;
  uint8 reserved1;
  uint8 preferred_pm_profile;
  uint16 sci_int;
  uint32 smi_cmd;
  uint8 acpi_enable;
  uint8 acpi_disable;
  uint8 s4bios_req;
  uint8 pstate_cnt;
  uint32 pm1a_evt_blk;
  uint32 pm1b_evt_blk;
  uint32 pm1a_cnt_blk;
  uint32 pm1b_cnt_blk;
  uint32 pm2_cnt_blk;
  uint32 pm_tmr_blk;
  uint32 gpe0_blk;
  uint32 gpe1_blk;
  uint8 pm1_evt_len;
  uint8 pm1_cnt_len;
  uint8 pm2_cnt_len;
  uint8 pm_tmr_len;
  uint8 gpe0_blk_len;
  uint8 gpe1_blk_len;
  uint8 gpe1_base;
  uint8 cst_cnt;
  uint16 p_lvl2_lat;
  uint16 p_lvl3_lat;
  uint16 flush_size;
  uint16 flush_stride;
  uint8 duty_offset;
  uint8 duty_width;
  uint8 day_alrm;
  uint8 mon_alrm;
  uint8 century;
  uint16 iapc_boot_arch;
  uint8 reserved2;
  uint32 flags;
  AcpiGas reset_reg;
  uint8 reset_value;
  uint8 reserved3[3];
  uint64 x_firmware_ctrl;
  uint64 x_dsdt;
  AcpiGas x_pm1a_evt_blk;
  AcpiGas x_pm1b_evt_blk;
  AcpiGas x_pm1a_cnt_blk;
  AcpiGas x_pm1b_cnt_blk;
} __attribute__((packed));

// ACPI Multiple APIC Description Table (MADT).
struct AcpiMadt {
  AcpiTableHeader header;
  uint32 lapic_address;
  uint32 flags;
} __attribute__((packed));

// Entry header for MADT records.
struct AcpiMadtEntryHeader {
  uint8 type;
  uint8 length;
} __attribute__((packed));

// Type 0: Processor Local APIC.
struct AcpiMadtProcessorLapic {
  AcpiMadtEntryHeader header;
  uint8 acpi_processor_id;
  uint8 apic_id;
  uint32 flags;
} __attribute__((packed));

// Type 1: I/O APIC.
struct AcpiMadtIoApic {
  AcpiMadtEntryHeader header;
  uint8 io_apic_id;
  uint8 reserved;
  uint32 io_apic_address;
  uint32 global_system_interrupt_base;
} __attribute__((packed));

// Type 5: 64-bit Local APIC Address Override.
struct AcpiMadtLapicAddressOverride {
  AcpiMadtEntryHeader header;
  uint16 reserved;
  uint64 lapic_address;
} __attribute__((packed));

// Discovered ACPI power management state.
uint32 g_pm1a_cnt_blk = 0;
uint32 g_pm1b_cnt_blk = 0;
uint32 g_smi_cmd = 0;
uint8 g_acpi_enable = 0;
uint8 g_slp_typa = 0;
uint8 g_slp_typb = 0;
bool g_has_s5 = false;

uint16 g_reset_port = 0;
uint8 g_reset_value = 0;
bool g_has_reset = false;

size_t g_rsdp_physical_address = 0;

// Discovered multiprocessing hardware state.
constexpr size_t kMaxDiscoveredCores = kMaxCores;
size_t g_discovered_core_count = 1;
uint32 g_core_apic_ids[kMaxDiscoveredCores] = {0};
size_t g_lapic_physical_address = 0xFEE00000;
size_t g_io_apic_physical_address = 0;

uint8 ParseAmlInteger(const uint8* data, size_t length, size_t& offset) {
  if (offset >= length) return 0;
  auto type = static_cast<AmlIntegerType>(data[offset++]);
  switch (type) {
    case AmlIntegerType::Zero:
      return 0;
    case AmlIntegerType::One:
      return 1;
    case AmlIntegerType::Ones:
      return 0xFF;
    case AmlIntegerType::Byte: {
      if (offset < length) return data[offset++];
      return 0;
    }
    case AmlIntegerType::Word: {
      uint8 val = 0;
      if (offset < length) val = data[offset++];
      if (offset < length) offset++;
      return val;
    }
    case AmlIntegerType::DWord: {
      uint8 val = 0;
      if (offset < length) val = data[offset++];
      offset += 3;
      return val;
    }
    default:
      return 0;
  }
}

#ifndef TEST
const AcpiTableHeader* MapAcpiTable(size_t phys_addr, size_t& mapped_pages,
                                    size_t& mapped_base_virt) {
  if (phys_addr == 0) return nullptr;

  size_t page_offset = phys_addr & (kPageSize - 1);
  size_t aligned_phys = phys_addr & ~(kPageSize - 1);

  mapped_pages = 1;
  mapped_base_virt = KernelAddressSpace().MapPhysicalPages(aligned_phys, 1);
  if (mapped_base_virt == kOutOfMemory) return nullptr;

  const auto* header =
      reinterpret_cast<const AcpiTableHeader*>(mapped_base_virt + page_offset);
  uint32 length = header->length;
  if (length < sizeof(AcpiTableHeader)) {
    KernelAddressSpace().FreePages(mapped_base_virt, 1);
    mapped_base_virt = 0;
    return nullptr;
  }

  size_t total_pages = (page_offset + length + kPageSize - 1) / kPageSize;
  if (total_pages > 1) {
    KernelAddressSpace().FreePages(mapped_base_virt, 1);
    mapped_pages = total_pages;
    mapped_base_virt =
        KernelAddressSpace().MapPhysicalPages(aligned_phys, total_pages);
    if (mapped_base_virt == kOutOfMemory) {
      mapped_base_virt = 0;
      return nullptr;
    }
    header = reinterpret_cast<const AcpiTableHeader*>(mapped_base_virt +
                                                      page_offset);
  }

  if (!ValidateAcpiTableChecksum(header, length)) {
    KernelAddressSpace().FreePages(mapped_base_virt, mapped_pages);
    mapped_base_virt = 0;
    return nullptr;
  }

  return header;
}

void UnmapAcpiTable(size_t mapped_base_virt, size_t mapped_pages) {
  if (mapped_base_virt != 0 && mapped_pages != 0)
    KernelAddressSpace().FreePages(mapped_base_virt, mapped_pages);
}

size_t ScanMemoryForRsdp(size_t phys_start, size_t length) {
  size_t pages = (length + kPageSize - 1) / kPageSize;
  size_t virt_base = KernelAddressSpace().MapPhysicalPages(phys_start, pages);
  if (virt_base == kOutOfMemory) return 0;

  size_t found_phys = 0;
  for (size_t offset = 0; offset + sizeof(AcpiRsdpDescriptor) <= length;
       offset += kRsdpAlign) {
    const auto* rsdp =
        reinterpret_cast<const AcpiRsdpDescriptor*>(virt_base + offset);
    if (rsdp->signature[0] == 'R' && rsdp->signature[1] == 'S' &&
        rsdp->signature[2] == 'D' && rsdp->signature[3] == ' ' &&
        rsdp->signature[4] == 'P' && rsdp->signature[5] == 'T' &&
        rsdp->signature[6] == 'R' && rsdp->signature[7] == ' ') {
      if (ValidateAcpiTableChecksum(rsdp, sizeof(AcpiRsdpDescriptor))) {
        found_phys = phys_start + offset;
        break;
      }
    }
  }

  KernelAddressSpace().FreePages(virt_base, pages);
  return found_phys;
}

size_t FindRsdpPhysicalAddress() {
  // Check Multiboot tags first.
  multiboot_info* higher_half_multiboot_info =
      reinterpret_cast<multiboot_info*>((size_t)&MultibootInfo +
                                        kVirtualMemoryOffset);

  size_t multiboot_addr =
      higher_half_multiboot_info->addr + kVirtualMemoryOffset;
  size_t multiboot_total_size = *reinterpret_cast<uint32*>(multiboot_addr);
  size_t multiboot_end = multiboot_addr + multiboot_total_size;

  auto* tag = reinterpret_cast<multiboot_tag*>(multiboot_addr + 8);
  for (; tag->type != MULTIBOOT_TAG_TYPE_END &&
         reinterpret_cast<size_t>(tag) < multiboot_end;
       tag = reinterpret_cast<multiboot_tag*>(reinterpret_cast<size_t>(tag) +
                                              ((tag->size + 7) & ~7))) {
    if (tag->size < 8) break;
    if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW) {
      const auto* acpi_tag =
          reinterpret_cast<const multiboot_tag_new_acpi*>(tag);
      const auto* rsdp =
          reinterpret_cast<const AcpiRsdpExtendedDescriptor*>(acpi_tag->rsdp);
      if (ValidateAcpiTableChecksum(rsdp, sizeof(AcpiRsdpDescriptor))) {
        if (rsdp->first_part.revision >= 2) {
          size_t rsdp_length = rsdp->length;
          if (rsdp_length > sizeof(AcpiRsdpExtendedDescriptor))
            rsdp_length = sizeof(AcpiRsdpExtendedDescriptor);
          if (ValidateAcpiTableChecksum(rsdp, rsdp_length))
            return reinterpret_cast<size_t>(acpi_tag->rsdp) -
                   kVirtualMemoryOffset;
        } else {
          return reinterpret_cast<size_t>(acpi_tag->rsdp) -
                 kVirtualMemoryOffset;
        }
      }
    } else if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD) {
      const auto* acpi_tag =
          reinterpret_cast<const multiboot_tag_old_acpi*>(tag);
      const auto* rsdp =
          reinterpret_cast<const AcpiRsdpDescriptor*>(acpi_tag->rsdp);
      if (ValidateAcpiTableChecksum(rsdp, sizeof(AcpiRsdpDescriptor)))
        return reinterpret_cast<size_t>(acpi_tag->rsdp) - kVirtualMemoryOffset;
    }
  }

  // Fallback to scanning EBDA.
  size_t page0 = KernelAddressSpace().MapPhysicalPages(0, 1);
  if (page0 != kOutOfMemory) {
    uint16 ebda_seg =
        *reinterpret_cast<const uint16*>(page0 + kEbdaSegmentPointerPhys);
    KernelAddressSpace().FreePages(page0, 1);
    size_t ebda_phys = static_cast<size_t>(ebda_seg) << 4;
    if (ebda_phys != 0 && ebda_phys < 0x100000) {
      size_t found = ScanMemoryForRsdp(ebda_phys, 1024);
      if (found != 0) return found;
    }
  }

  // Fallback to scanning BIOS ROM.
  return ScanMemoryForRsdp(kBiosRomStartPhys, kBiosRomLength);
}
#endif

}  // namespace

bool ValidateAcpiTableChecksum(const void* table, size_t length) {
  const auto* bytes = reinterpret_cast<const uint8*>(table);
  uint8 sum = 0;
  for (size_t i = 0; i < length; i++) sum += bytes[i];
  return sum == 0;
}

bool ParseMadtTable(const void* table_data, size_t length,
                    size_t& lapic_address, size_t& core_count,
                    uint32* core_apic_ids, size_t max_cores,
                    size_t& io_apic_address) {
  if (table_data == nullptr || length < sizeof(AcpiMadt)) return false;
  const auto* madt = reinterpret_cast<const AcpiMadt*>(table_data);
  lapic_address = madt->lapic_address;
  core_count = 0;
  io_apic_address = 0;

  size_t offset = sizeof(AcpiMadt);
  const auto* bytes = reinterpret_cast<const uint8*>(table_data);

  while (offset + sizeof(AcpiMadtEntryHeader) <= length) {
    const auto* entry =
        reinterpret_cast<const AcpiMadtEntryHeader*>(bytes + offset);
    if (entry->length < sizeof(AcpiMadtEntryHeader) ||
        offset + entry->length > length) {
      break;
    }

    if (entry->type == 0) {
      // Type 0: Processor Local APIC
      if (entry->length >= sizeof(AcpiMadtProcessorLapic)) {
        const auto* lapic_entry =
            reinterpret_cast<const AcpiMadtProcessorLapic*>(entry);
        if ((lapic_entry->flags & 1) || (lapic_entry->flags & 2)) {
          if (core_count < max_cores) {
            core_apic_ids[core_count++] = lapic_entry->apic_id;
          }
        }
      }
    } else if (entry->type == 1) {
      // Type 1: I/O APIC
      if (entry->length >= sizeof(AcpiMadtIoApic)) {
        const auto* io_apic_entry =
            reinterpret_cast<const AcpiMadtIoApic*>(entry);
        if (io_apic_address == 0) {
          io_apic_address = io_apic_entry->io_apic_address;
        }
      }
    } else if (entry->type == 5) {
      // Type 5: 64-bit Local APIC Address Override
      if (entry->length >= sizeof(AcpiMadtLapicAddressOverride)) {
        const auto* override_entry =
            reinterpret_cast<const AcpiMadtLapicAddressOverride*>(entry);
        lapic_address = override_entry->lapic_address;
      }
    }

    offset += entry->length;
  }

  return core_count > 0;
}

size_t GetDiscoveredCoreCount() { return g_discovered_core_count; }

uint32 GetCoreApicId(size_t core_index) {
  if (core_index < g_discovered_core_count) return g_core_apic_ids[core_index];
  return 0;
}

size_t GetLocalApicPhysicalAddress() { return g_lapic_physical_address; }

size_t GetIoApicPhysicalAddress() { return g_io_apic_physical_address; }

bool ParseAmlS5Package(const uint8* data, size_t length, uint8& out_slp_typa,
                       uint8& out_slp_typb) {
  out_slp_typa = 0;
  out_slp_typb = 0;
  if (length < 7) return false;

  for (size_t i = 0; i + 6 < length; i++) {
    if (data[i] != '_' || data[i + 1] != 'S' || data[i + 2] != '5' ||
        data[i + 3] != '_')
      continue;

    bool valid_name_op = false;
    if (i >= 1 && data[i - 1] == 0x08) {
      valid_name_op = true;
    } else if (i >= 2 && data[i - 1] == 0x5C && data[i - 2] == 0x08) {
      valid_name_op = true;
    } else if (i >= 1 && (data[i - 1] == 0x5C || data[i - 1] == 0x2E ||
                          data[i - 1] == 0x2F)) {
      valid_name_op = true;
    }
    if (!valid_name_op) continue;

    if (data[i + 4] != 0x12) continue;

    size_t offset = i + 5;
    if (offset >= length) return false;

    uint8 lead = data[offset++];
    uint8 byte_count = (lead >> 6) & 3;
    offset += byte_count;
    if (offset >= length) return false;

    uint8 num_elements = data[offset++];
    if (num_elements < 1) return false;

    out_slp_typa = ParseAmlInteger(data, length, offset);
    if (num_elements >= 2) out_slp_typb = ParseAmlInteger(data, length, offset);
    return true;
  }
  return false;
}

void InitializeAcpi() {
#ifndef TEST
  g_rsdp_physical_address = FindRsdpPhysicalAddress();
  if (g_rsdp_physical_address == 0) {
    print << "ACPI: RSDP not found.\n";
    return;
  }

  size_t page_offset = g_rsdp_physical_address & (kPageSize - 1);
  size_t aligned_phys = g_rsdp_physical_address & ~(kPageSize - 1);
  size_t rsdp_pages =
      (page_offset + sizeof(AcpiRsdpExtendedDescriptor) + kPageSize - 1) /
      kPageSize;

  size_t rsdp_virt =
      KernelAddressSpace().MapPhysicalPages(aligned_phys, rsdp_pages);
  if (rsdp_virt == kOutOfMemory) return;

  const auto* rsdp = reinterpret_cast<const AcpiRsdpExtendedDescriptor*>(
      rsdp_virt + page_offset);

  size_t xsdt_phys = 0;
  size_t rsdt_phys = 0;
  if (rsdp->first_part.revision >= 2) {
    xsdt_phys = rsdp->xsdt_address;
    rsdt_phys = rsdp->first_part.rsdt_address;
  } else {
    rsdt_phys = rsdp->first_part.rsdt_address;
  }

  UnmapAcpiTable(rsdp_virt, rsdp_pages);

  size_t fadt_phys = 0;
  size_t madt_phys = 0;

  auto scan_table_entry = [&](size_t entry_phys) {
    size_t table_pages = 0;
    size_t table_virt = 0;
    const auto* table = MapAcpiTable(entry_phys, table_pages, table_virt);
    if (table != nullptr) {
      if (fadt_phys == 0 && MemoryEquals(table->signature, "FACP", 4)) {
        fadt_phys = entry_phys;
      } else if (madt_phys == 0 && MemoryEquals(table->signature, "APIC", 4)) {
        madt_phys = entry_phys;
      }
      UnmapAcpiTable(table_virt, table_pages);
    }
  };

  if (xsdt_phys != 0) {
    size_t xsdt_pages = 0;
    size_t xsdt_virt = 0;
    const auto* xsdt = MapAcpiTable(xsdt_phys, xsdt_pages, xsdt_virt);
    if (xsdt != nullptr && MemoryEquals(xsdt->signature, "XSDT", 4)) {
      size_t num_entries = (xsdt->length - sizeof(AcpiTableHeader)) / 8;
      const auto* entries = reinterpret_cast<const uint64*>(
          reinterpret_cast<const char*>(xsdt) + sizeof(AcpiTableHeader));
      for (size_t i = 0; i < num_entries; i++) {
        scan_table_entry(entries[i]);
        if (fadt_phys != 0 && madt_phys != 0) break;
      }
    }
    UnmapAcpiTable(xsdt_virt, xsdt_pages);
  }

  if ((fadt_phys == 0 || madt_phys == 0) && rsdt_phys != 0) {
    size_t rsdt_pages = 0;
    size_t rsdt_virt = 0;
    const auto* rsdt = MapAcpiTable(rsdt_phys, rsdt_pages, rsdt_virt);
    if (rsdt != nullptr && MemoryEquals(rsdt->signature, "RSDT", 4)) {
      size_t num_entries = (rsdt->length - sizeof(AcpiTableHeader)) / 4;
      const auto* entries = reinterpret_cast<const uint32*>(
          reinterpret_cast<const char*>(rsdt) + sizeof(AcpiTableHeader));
      for (size_t i = 0; i < num_entries; i++) {
        scan_table_entry(entries[i]);
        if (fadt_phys != 0 && madt_phys != 0) break;
      }
    }
    UnmapAcpiTable(rsdt_virt, rsdt_pages);
  }

  if (madt_phys != 0) {
    size_t madt_pages = 0;
    size_t madt_virt = 0;
    const auto* madt_header = MapAcpiTable(madt_phys, madt_pages, madt_virt);
    if (madt_header != nullptr) {
      size_t lapic_addr = g_lapic_physical_address;
      size_t core_count = 0;
      uint32 core_apic_ids[kMaxDiscoveredCores] = {0};
      size_t io_apic_addr = 0;
      if (ParseMadtTable(madt_header, madt_header->length, lapic_addr,
                         core_count, core_apic_ids, kMaxDiscoveredCores,
                         io_apic_addr)) {
        g_lapic_physical_address = lapic_addr;
        g_discovered_core_count = core_count;
        for (size_t i = 0; i < core_count; i++) {
          g_core_apic_ids[i] = core_apic_ids[i];
        }
        g_io_apic_physical_address = io_apic_addr;
      }
      UnmapAcpiTable(madt_virt, madt_pages);
    }
  }

  if (fadt_phys == 0) {
    print << "ACPI: FADT not found.\n";
    return;
  }

  size_t fadt_pages = 0;
  size_t fadt_virt = 0;
  const auto* fadt_header = MapAcpiTable(fadt_phys, fadt_pages, fadt_virt);
  if (fadt_header == nullptr) return;

  if (fadt_header->length < 89) {
    UnmapAcpiTable(fadt_virt, fadt_pages);
    return;
  }

  const auto* fadt = reinterpret_cast<const AcpiFadt*>(fadt_header);
  g_pm1a_cnt_blk = fadt->pm1a_cnt_blk;
  g_pm1b_cnt_blk = fadt->pm1b_cnt_blk;
  g_smi_cmd = fadt->smi_cmd;
  g_acpi_enable = fadt->acpi_enable;

  size_t dsdt_phys = fadt->dsdt;
  if (fadt->header.length >= 148 && fadt->x_dsdt != 0) dsdt_phys = fadt->x_dsdt;

  if (fadt->header.length >= 129 && (fadt->flags & kResetRegSupportedFlag)) {
    if (fadt->reset_reg.address_space_id == kSystemIoAddressSpaceId &&
        fadt->reset_reg.address != 0) {
      g_reset_port = static_cast<uint16>(fadt->reset_reg.address);
      g_reset_value = fadt->reset_value;
      g_has_reset = true;
    }
  }

  UnmapAcpiTable(fadt_virt, fadt_pages);

  if (dsdt_phys != 0) {
    size_t dsdt_pages = 0;
    size_t dsdt_virt = 0;
    const auto* dsdt = MapAcpiTable(dsdt_phys, dsdt_pages, dsdt_virt);
    if (dsdt != nullptr) {
      if (ParseAmlS5Package(
              reinterpret_cast<const uint8*>(dsdt) + sizeof(AcpiTableHeader),
              dsdt->length - sizeof(AcpiTableHeader), g_slp_typa, g_slp_typb)) {
        g_has_s5 = true;
      }
      UnmapAcpiTable(dsdt_virt, dsdt_pages);
    }
  }

  print << "ACPI initialized: Cores=" << g_discovered_core_count
        << " LAPIC=" << NumberFormat::Hexidecimal << g_lapic_physical_address
        << " IOAPIC=" << NumberFormat::Hexidecimal << g_io_apic_physical_address
        << " PM1a=" << NumberFormat::Hexidecimal
        << static_cast<size_t>(g_pm1a_cnt_blk)
        << " PM1b=" << static_cast<size_t>(g_pm1b_cnt_blk)
        << " S5=" << (g_has_s5 ? "yes" : "no")
        << " Reset=" << (g_has_reset ? "yes" : "no") << "\n";
#endif
}

void AcpiPowerOff() {
#ifndef TEST
  if (!g_has_s5 || g_pm1a_cnt_blk == 0) return;

  if ((ReadIO16Bits(static_cast<uint16>(g_pm1a_cnt_blk)) & kSciEnableBit) ==
          0 &&
      g_smi_cmd != 0 && g_acpi_enable != 0) {
    WriteIOByte(static_cast<uint16>(g_smi_cmd), g_acpi_enable);
    for (int i = 0; i < kAcpiEnableTimeoutIterations; i++) {
      if ((ReadIO16Bits(static_cast<uint16>(g_pm1a_cnt_blk)) & kSciEnableBit) !=
          0)
        break;
      (void)ReadIOByte(0x80);
    }
  }

  uint16 val_a = static_cast<uint16>((g_slp_typa << 10) | kSleepEnableBit);
  WriteIO16Bits(static_cast<uint16>(g_pm1a_cnt_blk), val_a);

  if (g_pm1b_cnt_blk != 0) {
    uint16 val_b = static_cast<uint16>((g_slp_typb << 10) | kSleepEnableBit);
    WriteIO16Bits(static_cast<uint16>(g_pm1b_cnt_blk), val_b);
  }
#endif
}

void AcpiReset() {
#ifndef TEST
  if (g_has_reset && g_reset_port != 0)
    WriteIOByte(g_reset_port, g_reset_value);
#endif
}

bool HasAcpiS5() { return g_has_s5; }

bool HasAcpiReset() { return g_has_reset; }

void PopulateRegistersWithAcpiDetails(Registers& regs) {
  regs.rax = (g_pm1a_cnt_blk & 0xFFFF) | ((g_pm1b_cnt_blk & 0xFFFF) << 16);
  regs.rbx = (g_slp_typa & 0xFF) | ((g_slp_typb & 0xFF) << 8) |
             ((g_has_s5 ? 1 : 0) << 16);
  regs.rdx = (g_smi_cmd & 0xFFFF) | ((g_acpi_enable & 0xFF) << 16);
  regs.rsi = (g_reset_port & 0xFFFF) | ((g_reset_value & 0xFF) << 16) |
             ((g_has_reset ? 1 : 0) << 24);
  regs.r8 = g_rsdp_physical_address;
}

}  // namespace hardware
