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

#include "testing.h"

using hardware::ParseAmlS5Package;
using hardware::ParseMadtTable;
using hardware::ValidateAcpiTableChecksum;

TEST(AcpiParseS5PackageBytePrefixTest) {
  // AML: Name(_S5, Package(2) { 5, 5 })
  // 0x08, '_', 'S', '5', '_', 0x12 (PackageOp), 0x06 (len), 0x02 (num elements),
  // 0x0A, 0x05 (BytePrefix 5), 0x0A, 0x05 (BytePrefix 5)
  const uint8 aml_data[] = {
      0x08, '_', 'S', '5', '_', 0x12, 0x06, 0x02, 0x0A, 0x05, 0x0A, 0x05};

  uint8 slp_typa = 0;
  uint8 slp_typb = 0;
  bool success =
      ParseAmlS5Package(aml_data, sizeof(aml_data), slp_typa, slp_typb);

  ASSERT(success, true);
  ASSERT(slp_typa, 5);
  ASSERT(slp_typb, 5);
}

TEST(AcpiParseS5PackageRootPrefixTest) {
  // AML: Name(\_S5, Package(2) { 0, 7 })
  // 0x08, 0x5C (\), '_', 'S', '5', '_', 0x12 (PackageOp), 0x05 (len), 0x02 (num elements),
  // 0x00 (ZeroOp), 0x0A, 0x07 (BytePrefix 7)
  const uint8 aml_data[] = {0x08, 0x5C, '_', 'S',  '5', '_',
                            0x12, 0x05, 0x02, 0x00, 0x0A, 0x07};

  uint8 slp_typa = 0;
  uint8 slp_typb = 0;
  bool success =
      ParseAmlS5Package(aml_data, sizeof(aml_data), slp_typa, slp_typb);

  ASSERT(success, true);
  ASSERT(slp_typa, 0);
  ASSERT(slp_typb, 7);
}

TEST(AcpiParseS5PackageWordPrefixTest) {
  // AML: Name(_S5, Package(2) { 0x0003, 0x0004 })
  // 0x08, '_', 'S', '5', '_', 0x12 (PackageOp), 0x08 (len), 0x02 (num elements),
  // 0x0B, 0x03, 0x00 (WordPrefix 3), 0x0B, 0x04, 0x00 (WordPrefix 4)
  const uint8 aml_data[] = {0x08, '_',  'S',  '5',  '_',  0x12, 0x08,
                            0x02, 0x0B, 0x03, 0x00, 0x0B, 0x04, 0x00};

  uint8 slp_typa = 0;
  uint8 slp_typb = 0;
  bool success =
      ParseAmlS5Package(aml_data, sizeof(aml_data), slp_typa, slp_typb);

  ASSERT(success, true);
  ASSERT(slp_typa, 3);
  ASSERT(slp_typb, 4);
}

TEST(AcpiParseS5PackageOneAndOnesOpTest) {
  // AML: Name(_S5, Package(2) { 1, Ones })
  // 0x08, '_', 'S', '5', '_', 0x12 (PackageOp), 0x04 (len), 0x02 (num
  // elements), 0x01 (OneOp), 0xFF (OnesOp)
  const uint8 aml_data[] = {0x08, '_',  'S',  '5',  '_',
                            0x12, 0x04, 0x02, 0x01, 0xFF};

  uint8 slp_typa = 0;
  uint8 slp_typb = 0;
  bool success =
      ParseAmlS5Package(aml_data, sizeof(aml_data), slp_typa, slp_typb);

  ASSERT(success, true);
  ASSERT(slp_typa, 1);
  ASSERT(slp_typb, 0xFF);
}

TEST(AcpiParseS5PackageDWordPrefixTest) {
  // AML: Name(_S5, Package(2) { 0x00000005, 0x00000006 })
  // 0x08, '_', 'S', '5', '_', 0x12 (PackageOp), 0x0C (len), 0x02 (num
  // elements), 0x0C, 0x05, 0x00, 0x00, 0x00 (DWordPrefix 5), 0x0C, 0x06, 0x00,
  // 0x00, 0x00 (DWordPrefix 6)
  const uint8 aml_data[] = {0x08, '_',  'S',  '5',  '_',  0x12,
                            0x0C, 0x02, 0x0C, 0x05, 0x00, 0x00,
                            0x00, 0x0C, 0x06, 0x00, 0x00, 0x00};

  uint8 slp_typa = 0;
  uint8 slp_typb = 0;
  bool success =
      ParseAmlS5Package(aml_data, sizeof(aml_data), slp_typa, slp_typb);

  ASSERT(success, true);
  ASSERT(slp_typa, 5);
  ASSERT(slp_typb, 6);
}

TEST(AcpiParseS5PackageInvalidAmlTest) {
  const uint8 empty_data[] = {0x00, 0x01, 0x02};
  uint8 slp_typa = 0;
  uint8 slp_typb = 0;

  bool success1 =
      ParseAmlS5Package(empty_data, sizeof(empty_data), slp_typa, slp_typb);
  ASSERT(success1, false);

  // Missing PackageOp
  const uint8 broken_aml[] = {0x08, '_', 'S', '5', '_', 0x00, 0x01};
  bool success2 =
      ParseAmlS5Package(broken_aml, sizeof(broken_aml), slp_typa, slp_typb);
  ASSERT(success2, false);
}

TEST(AcpiValidateChecksumTest) {
  uint8 valid_table[4] = {0x10, 0x20, 0x30, 0xA0};  // 0x10 + 0x20 + 0x30 + 0xA0 = 0x100 = 0 mod 256
  ASSERT(ValidateAcpiTableChecksum(valid_table, sizeof(valid_table)), true);

  uint8 invalid_table[4] = {0x10, 0x20, 0x30, 0xA1};
  ASSERT(ValidateAcpiTableChecksum(invalid_table, sizeof(invalid_table)), false);
}

TEST(AcpiParseMadtTableTest) {
  // Synthesize an ACPI MADT table with 4 cores (one disabled) and 1 IO APIC.
  // Header: 44 bytes
  // Entry 0 (Type 0, Core 0, enabled): 8 bytes
  // Entry 1 (Type 0, Core 1, enabled): 8 bytes
  // Entry 2 (Type 0, Core 2, disabled): 8 bytes
  // Entry 3 (Type 0, Core 3, enabled): 8 bytes
  // Entry 4 (Type 1, IO APIC): 12 bytes
  // Total length = 44 + 4*8 + 12 = 88 bytes.
  uint8 madt_data[88] = {0};

  // Signature: "APIC"
  madt_data[0] = 'A';
  madt_data[1] = 'P';
  madt_data[2] = 'I';
  madt_data[3] = 'C';

  // Length: 88
  *reinterpret_cast<uint32*>(&madt_data[4]) = sizeof(madt_data);

  // Local APIC address: 0xFEE00000
  *reinterpret_cast<uint32*>(&madt_data[36]) = 0xFEE00000;

  // Entry 0: Type 0, length 8, proc_id 0, apic_id 10, flags 1 (enabled)
  size_t offset = 44;
  madt_data[offset] = 0;
  madt_data[offset + 1] = 8;
  madt_data[offset + 2] = 0;
  madt_data[offset + 3] = 10;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 1;
  offset += 8;

  // Entry 1: Type 0, length 8, proc_id 1, apic_id 11, flags 1 (enabled)
  madt_data[offset] = 0;
  madt_data[offset + 1] = 8;
  madt_data[offset + 2] = 1;
  madt_data[offset + 3] = 11;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 1;
  offset += 8;

  // Entry 2: Type 0, length 8, proc_id 2, apic_id 12, flags 0 (disabled)
  madt_data[offset] = 0;
  madt_data[offset + 1] = 8;
  madt_data[offset + 2] = 2;
  madt_data[offset + 3] = 12;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 0;
  offset += 8;

  // Entry 3: Type 0, length 8, proc_id 3, apic_id 13, flags 2 (online capable)
  madt_data[offset] = 0;
  madt_data[offset + 1] = 8;
  madt_data[offset + 2] = 3;
  madt_data[offset + 3] = 13;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 2;
  offset += 8;

  // Entry 4: Type 1, length 12, io_apic_id 1, io_apic_address 0xFEC00000
  madt_data[offset] = 1;
  madt_data[offset + 1] = 12;
  madt_data[offset + 2] = 1;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 0xFEC00000;

  size_t lapic_address = 0;
  size_t core_count = 0;
  uint32 core_apic_ids[16] = {0};
  size_t io_apic_address = 0;

  bool success = ParseMadtTable(madt_data, sizeof(madt_data), lapic_address,
                                core_count, core_apic_ids, 16, io_apic_address);

  ASSERT(success, true);
  ASSERT(lapic_address, static_cast<size_t>(0xFEE00000));
  ASSERT(core_count, static_cast<size_t>(3));
  ASSERT(core_apic_ids[0], static_cast<uint32>(10));
  ASSERT(core_apic_ids[1], static_cast<uint32>(11));
  ASSERT(core_apic_ids[2], static_cast<uint32>(13));
  ASSERT(io_apic_address, static_cast<size_t>(0xFEC00000));
}

TEST(AcpiParseMadtLapicOverrideTest) {
  // MADT with a 64-bit LAPIC address override (Type 5).
  uint8 madt_data[44 + 8 + 12] = {0};

  // Header
  madt_data[0] = 'A';
  madt_data[1] = 'P';
  madt_data[2] = 'I';
  madt_data[3] = 'C';
  *reinterpret_cast<uint32*>(&madt_data[4]) = sizeof(madt_data);
  *reinterpret_cast<uint32*>(&madt_data[36]) = 0xFEE00000;

  // Type 0 entry (Core 0)
  size_t offset = 44;
  madt_data[offset] = 0;
  madt_data[offset + 1] = 8;
  madt_data[offset + 3] = 0;
  *reinterpret_cast<uint32*>(&madt_data[offset + 4]) = 1;
  offset += 8;

  // Type 5 entry: 64-bit LAPIC address override
  madt_data[offset] = 5;
  madt_data[offset + 1] = 12;
  *reinterpret_cast<uint64*>(&madt_data[offset + 4]) = 0x2FEE00000ULL;

  size_t lapic_address = 0;
  size_t core_count = 0;
  uint32 core_apic_ids[16] = {0};
  size_t io_apic_address = 0;

  bool success = ParseMadtTable(madt_data, sizeof(madt_data), lapic_address,
                                core_count, core_apic_ids, 16, io_apic_address);

  ASSERT(success, true);
  ASSERT(core_count, static_cast<size_t>(1));
  ASSERT(lapic_address, static_cast<size_t>(0x2FEE00000ULL));
}
