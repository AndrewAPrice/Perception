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

#include "perception/disk/crc32.h"

#include <cstdint>
#include <vector>

#include "testing.h"

namespace {

// Expected CRC32 checksum for standard ASCII test sequence "123456789".
constexpr uint32_t kExpectedCrc32ForNumbers = 0xCBF43926;

// Expected CRC32 checksum for pangram "The quick brown fox jumps over the lazy dog".
constexpr uint32_t kExpectedCrc32ForPangram = 0x414FA339;

// Size of large buffer in bytes (64 KiB).
constexpr size_t kLargeBufferSize = 65536;

using ::perception::disk::CalculateCrc32;

TEST(Crc32EmptyBuffer) {
  EXPECT((uint32_t)0x00000000, CalculateCrc32(nullptr, 0));

  uint8_t dummy = 0;
  EXPECT((uint32_t)0x00000000, CalculateCrc32(&dummy, 0));
}

TEST(Crc32StandardTestVector) {
  const char test_data[] = "123456789";
  EXPECT(kExpectedCrc32ForNumbers,
         CalculateCrc32(reinterpret_cast<const uint8_t*>(test_data), 9));
}

TEST(Crc32PangramVector) {
  const char pangram[] = "The quick brown fox jumps over the lazy dog";
  EXPECT(kExpectedCrc32ForPangram,
         CalculateCrc32(reinterpret_cast<const uint8_t*>(pangram), 43));
}

TEST(Crc32BitFlipSensitivity) {
  std::vector<uint8_t> data(128, 0xAB);
  uint32_t original_crc = CalculateCrc32(data.data(), data.size());

  for (size_t byte_idx = 0; byte_idx < data.size(); byte_idx += 16) {
    for (int bit = 0; bit < 8; bit++) {
      data[byte_idx] ^= (1 << bit);
      uint32_t mutated_crc = CalculateCrc32(data.data(), data.size());
      EXPECT(false, original_crc == mutated_crc);
      data[byte_idx] ^= (1 << bit);
    }
  }
}

TEST(Crc32AllZerosAndOnes) {
  std::vector<uint8_t> zeros(512, 0);
  uint32_t zeros_crc = CalculateCrc32(zeros.data(), zeros.size());
  EXPECT(false, zeros_crc == 0);

  std::vector<uint8_t> ones(512, 0xFF);
  uint32_t ones_crc = CalculateCrc32(ones.data(), ones.size());
  EXPECT(false, ones_crc == 0);
  EXPECT(false, zeros_crc == ones_crc);
}

TEST(Crc32LargeBuffer) {
  std::vector<uint8_t> buffer(kLargeBufferSize);
  for (size_t i = 0; i < kLargeBufferSize; i++)
    buffer[i] = static_cast<uint8_t>(i & 0xFF);

  uint32_t crc = CalculateCrc32(buffer.data(), buffer.size());
  EXPECT(false, crc == 0);

  buffer[kLargeBufferSize - 1] ^= 0x01;
  uint32_t mutated_crc = CalculateCrc32(buffer.data(), buffer.size());
  EXPECT(false, crc == mutated_crc);
}

}  // namespace
