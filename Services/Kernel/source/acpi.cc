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

#include "acpi.h"

#ifndef TEST
#include "../../../third_party/multiboot2.h"
#include "io.h"
#include "physical_allocator.h"
#include "text_terminal.h"
#include "virtual_address_space.h"
#include "virtual_allocator.h"
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

// Discovered ACPI power management state.
uint32 pm1a_cnt_blk = 0;
uint32 pm1b_cnt_blk = 0;
uint32 smi_cmd = 0;
uint8 acpi_enable = 0;
uint8 slp_typa = 0;
uint8 slp_typb = 0;
bool has_s5 = false;

uint16 reset_port = 0;
uint8 reset_value = 0;
bool has_reset = false;

size_t rsdp_physical_address = 0;

uint8 ParseAmlInteger(const uint8* data, size_t length, size_t& offset) {
  if (offset >= length) return 0;
  uint8 op = data[offset++];
  if (op == 0x00) return 0;
  if (op == 0x01) return 1;
  if (op == 0xFF) return 0xFF;
  if (op == 0x0A) {
    if (offset < length) return data[offset++];
    return 0;
  }
  if (op == 0x0B) {
    uint8 val = 0;
    if (offset < length) val = data[offset++];
    if (offset < length) offset++;
    return val;
  }
  if (op == 0x0C) {
    uint8 val = 0;
    if (offset < length) val = data[offset++];
    offset += 3;
    return val;
  }
  return 0;
}

#ifndef TEST
const AcpiTableHeader* MapAcpiTable(size_t phys_addr, size_t& mapped_pages,
                                    size_t& mapped_base_virt) {
  if (phys_addr == 0) return nullptr;

  size_t page_offset = phys_addr & (PAGE_SIZE - 1);
  size_t aligned_phys = phys_addr & ~(PAGE_SIZE - 1);

  mapped_pages = 1;
  mapped_base_virt = KernelAddressSpace().MapPhysicalPages(aligned_phys, 1);
  if (mapped_base_virt == OUT_OF_MEMORY) return nullptr;

  const auto* header =
      reinterpret_cast<const AcpiTableHeader*>(mapped_base_virt + page_offset);
  uint32 length = header->length;
  if (length < sizeof(AcpiTableHeader)) {
    KernelAddressSpace().FreePages(mapped_base_virt, 1);
    mapped_base_virt = 0;
    return nullptr;
  }

  size_t total_pages = (page_offset + length + PAGE_SIZE - 1) / PAGE_SIZE;
  if (total_pages > 1) {
    KernelAddressSpace().FreePages(mapped_base_virt, 1);
    mapped_pages = total_pages;
    mapped_base_virt =
        KernelAddressSpace().MapPhysicalPages(aligned_phys, total_pages);
    if (mapped_base_virt == OUT_OF_MEMORY) {
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
  size_t pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
  size_t virt_base = KernelAddressSpace().MapPhysicalPages(phys_start, pages);
  if (virt_base == OUT_OF_MEMORY) return 0;

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
                                        VIRTUAL_MEMORY_OFFSET);

  size_t multiboot_addr =
      higher_half_multiboot_info->addr + VIRTUAL_MEMORY_OFFSET;
  size_t multiboot_total_size = *reinterpret_cast<uint32*>(multiboot_addr);
  size_t multiboot_end = multiboot_addr + multiboot_total_size;

  auto* tag = reinterpret_cast<multiboot_tag*>(multiboot_addr + 8);
  for (; tag->type != MULTIBOOT_TAG_TYPE_END &&
         reinterpret_cast<size_t>(tag) < multiboot_end;
       tag = reinterpret_cast<multiboot_tag*>(reinterpret_cast<size_t>(tag) +
                                              ((tag->size + 7) & ~7))) {
    if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_NEW) {
      const auto* acpi_tag =
          reinterpret_cast<const multiboot_tag_new_acpi*>(tag);
      const auto* rsdp =
          reinterpret_cast<const AcpiRsdpExtendedDescriptor*>(acpi_tag->rsdp);
      if (ValidateAcpiTableChecksum(rsdp, sizeof(AcpiRsdpDescriptor))) {
        if (rsdp->first_part.revision >= 2) {
          if (ValidateAcpiTableChecksum(rsdp, rsdp->length))
            return reinterpret_cast<size_t>(acpi_tag->rsdp) -
                   VIRTUAL_MEMORY_OFFSET;
        } else {
          return reinterpret_cast<size_t>(acpi_tag->rsdp) -
                 VIRTUAL_MEMORY_OFFSET;
        }
      }
    } else if (tag->type == MULTIBOOT_TAG_TYPE_ACPI_OLD) {
      const auto* acpi_tag =
          reinterpret_cast<const multiboot_tag_old_acpi*>(tag);
      const auto* rsdp =
          reinterpret_cast<const AcpiRsdpDescriptor*>(acpi_tag->rsdp);
      if (ValidateAcpiTableChecksum(rsdp, sizeof(AcpiRsdpDescriptor)))
        return reinterpret_cast<size_t>(acpi_tag->rsdp) - VIRTUAL_MEMORY_OFFSET;
    }
  }

  // Fallback to scanning EBDA.
  size_t page0 = KernelAddressSpace().MapPhysicalPages(0, 1);
  if (page0 != OUT_OF_MEMORY) {
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
  rsdp_physical_address = FindRsdpPhysicalAddress();
  if (rsdp_physical_address == 0) {
    print << "ACPI: RSDP not found.\n";
    return;
  }

  size_t rsdp_pages = 0;
  size_t rsdp_virt = 0;
  size_t page_offset = rsdp_physical_address & (PAGE_SIZE - 1);
  size_t aligned_phys = rsdp_physical_address & ~(PAGE_SIZE - 1);

  rsdp_virt = KernelAddressSpace().MapPhysicalPages(aligned_phys, 1);
  if (rsdp_virt == OUT_OF_MEMORY) return;
  rsdp_pages = 1;

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
  if (xsdt_phys != 0) {
    size_t xsdt_pages = 0;
    size_t xsdt_virt = 0;
    const auto* xsdt = MapAcpiTable(xsdt_phys, xsdt_pages, xsdt_virt);
    if (xsdt != nullptr && xsdt->signature[0] == 'X' &&
        xsdt->signature[1] == 'S' && xsdt->signature[2] == 'D' &&
        xsdt->signature[3] == 'T') {
      size_t num_entries = (xsdt->length - sizeof(AcpiTableHeader)) / 8;
      const auto* entries = reinterpret_cast<const uint64*>(
          reinterpret_cast<const char*>(xsdt) + sizeof(AcpiTableHeader));
      for (size_t i = 0; i < num_entries; i++) {
        size_t table_pages = 0;
        size_t table_virt = 0;
        const auto* table = MapAcpiTable(entries[i], table_pages, table_virt);
        if (table != nullptr) {
          if (table->signature[0] == 'F' && table->signature[1] == 'A' &&
              table->signature[2] == 'C' && table->signature[3] == 'P') {
            fadt_phys = entries[i];
            UnmapAcpiTable(table_virt, table_pages);
            break;
          }
          UnmapAcpiTable(table_virt, table_pages);
        }
      }
    }
    UnmapAcpiTable(xsdt_virt, xsdt_pages);
  }

  if (fadt_phys == 0 && rsdt_phys != 0) {
    size_t rsdt_pages = 0;
    size_t rsdt_virt = 0;
    const auto* rsdt = MapAcpiTable(rsdt_phys, rsdt_pages, rsdt_virt);
    if (rsdt != nullptr && rsdt->signature[0] == 'R' &&
        rsdt->signature[1] == 'S' && rsdt->signature[2] == 'D' &&
        rsdt->signature[3] == 'T') {
      size_t num_entries = (rsdt->length - sizeof(AcpiTableHeader)) / 4;
      const auto* entries = reinterpret_cast<const uint32*>(
          reinterpret_cast<const char*>(rsdt) + sizeof(AcpiTableHeader));
      for (size_t i = 0; i < num_entries; i++) {
        size_t table_pages = 0;
        size_t table_virt = 0;
        const auto* table = MapAcpiTable(entries[i], table_pages, table_virt);
        if (table != nullptr) {
          if (table->signature[0] == 'F' && table->signature[1] == 'A' &&
              table->signature[2] == 'C' && table->signature[3] == 'P') {
            fadt_phys = entries[i];
            UnmapAcpiTable(table_virt, table_pages);
            break;
          }
          UnmapAcpiTable(table_virt, table_pages);
        }
      }
    }
    UnmapAcpiTable(rsdt_virt, rsdt_pages);
  }

  if (fadt_phys == 0) {
    print << "ACPI: FADT not found.\n";
    return;
  }

  size_t fadt_pages = 0;
  size_t fadt_virt = 0;
  const auto* fadt_header = MapAcpiTable(fadt_phys, fadt_pages, fadt_virt);
  if (fadt_header == nullptr) return;

  const auto* fadt = reinterpret_cast<const AcpiFadt*>(fadt_header);
  pm1a_cnt_blk = fadt->pm1a_cnt_blk;
  pm1b_cnt_blk = fadt->pm1b_cnt_blk;
  smi_cmd = fadt->smi_cmd;
  acpi_enable = fadt->acpi_enable;

  size_t dsdt_phys = fadt->dsdt;
  if (fadt->header.length >= 148 && fadt->x_dsdt != 0) dsdt_phys = fadt->x_dsdt;

  if (fadt->header.length >= 129 && (fadt->flags & kResetRegSupportedFlag)) {
    if (fadt->reset_reg.address_space_id == kSystemIoAddressSpaceId &&
        fadt->reset_reg.address != 0) {
      reset_port = static_cast<uint16>(fadt->reset_reg.address);
      reset_value = fadt->reset_value;
      has_reset = true;
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
              dsdt->length - sizeof(AcpiTableHeader), slp_typa, slp_typb)) {
        has_s5 = true;
      }
      UnmapAcpiTable(dsdt_virt, dsdt_pages);
    }
  }

  print << "ACPI initialized: PM1a=" << NumberFormat::Hexidecimal
        << static_cast<size_t>(pm1a_cnt_blk)
        << " PM1b=" << static_cast<size_t>(pm1b_cnt_blk)
        << " S5=" << (has_s5 ? "yes" : "no")
        << " Reset=" << (has_reset ? "yes" : "no") << "\n";
#endif
}

void AcpiPowerOff() {
#ifndef TEST
  if (!has_s5 || pm1a_cnt_blk == 0) return;

  if ((ReadIO16Bits(static_cast<uint16>(pm1a_cnt_blk)) & kSciEnableBit) == 0 &&
      smi_cmd != 0 && acpi_enable != 0) {
    WriteIOByte(static_cast<uint16>(smi_cmd), acpi_enable);
    for (int i = 0; i < kAcpiEnableTimeoutIterations; i++) {
      if ((ReadIO16Bits(static_cast<uint16>(pm1a_cnt_blk)) & kSciEnableBit) !=
          0)
        break;
      (void)ReadIOByte(0x80);
    }
  }

  uint16 val_a = static_cast<uint16>((slp_typa << 10) | kSleepEnableBit);
  WriteIO16Bits(static_cast<uint16>(pm1a_cnt_blk), val_a);

  if (pm1b_cnt_blk != 0) {
    uint16 val_b = static_cast<uint16>((slp_typb << 10) | kSleepEnableBit);
    WriteIO16Bits(static_cast<uint16>(pm1b_cnt_blk), val_b);
  }
#endif
}

void AcpiReset() {
#ifndef TEST
  if (has_reset && reset_port != 0) WriteIOByte(reset_port, reset_value);
#endif
}

bool HasAcpiS5() { return has_s5; }

bool HasAcpiReset() { return has_reset; }

void PopulateRegistersWithAcpiDetails(Registers* regs) {
  regs->rax = (pm1a_cnt_blk & 0xFFFF) | ((pm1b_cnt_blk & 0xFFFF) << 16);
  regs->rbx =
      (slp_typa & 0xFF) | ((slp_typb & 0xFF) << 8) | ((has_s5 ? 1 : 0) << 16);
  regs->rdx = (smi_cmd & 0xFFFF) | ((acpi_enable & 0xFF) << 16);
  regs->rsi = (reset_port & 0xFFFF) | ((reset_value & 0xFF) << 16) |
              ((has_reset ? 1 : 0) << 24);
  regs->r8 = rsdp_physical_address;
}
