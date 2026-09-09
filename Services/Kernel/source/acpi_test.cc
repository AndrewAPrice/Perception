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
#include "testing.h"

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
