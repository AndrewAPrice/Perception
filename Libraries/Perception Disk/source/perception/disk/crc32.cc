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

#include <array>

namespace {

// Standard polynomial for IEEE 802.3 CRC32 in reversed representation.
constexpr uint32_t kCrc32Polynomial = 0xEDB88320;

// Precomputed CRC32 lookup table generator.
constexpr std::array<uint32_t, 256> GenerateCrcTable() {
  std::array<uint32_t, 256> table = {};
  for (uint32_t i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      if (crc & 1)
        crc = (crc >> 1) ^ kCrc32Polynomial;
      else
        crc >>= 1;
    }
    table[i] = crc;
  }
  return table;
}

// Precomputed table of CRC32 residues.
constexpr std::array<uint32_t, 256> kCrcTable = GenerateCrcTable();

}  // namespace

namespace perception {
namespace disk {

uint32_t CalculateCrc32(const uint8_t* data, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < length; i++)
    crc = (crc >> 8) ^ kCrcTable[(crc ^ data[i]) & 0xFF];
  return crc ^ 0xFFFFFFFF;
}

}  // namespace disk
}  // namespace perception
