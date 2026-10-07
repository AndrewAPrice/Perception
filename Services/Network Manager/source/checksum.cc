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

#include "checksum.h"

#include <span>

using ::perception::network::IpAddress;

namespace {

// Number of bits in a byte.
constexpr int kBitsPerByte = 8;

// Number of bits in a 16-bit word.
constexpr int kBitsPerWord = 16;

// Mask for the low 16 bits of a 32-bit accumulator.
constexpr uint32 kWordMask = 0xFFFF;

// Accumulates big-endian 16-bit words from `bytes` into `sum`.
uint32 AccumulateSpan(std::span<const uint8> bytes, uint32 sum) {
  for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
    sum += (static_cast<uint32>(bytes[i]) << kBitsPerByte) |
           static_cast<uint32>(bytes[i + 1]);
  }
  if (bytes.size() & 1)
    sum += static_cast<uint32>(bytes.back()) << kBitsPerByte;
  return sum;
}

}  // namespace

uint16 InternetChecksum(std::string_view data, uint32 initial_sum) {
  std::span<const uint8> bytes(reinterpret_cast<const uint8*>(data.data()),
                               data.size());
  uint32 sum = AccumulateSpan(bytes, initial_sum);
  while (sum >> kBitsPerWord)
    sum = (sum & kWordMask) + (sum >> kBitsPerWord);
  return static_cast<uint16>(~sum);
}

uint16 TransportChecksum(const IpAddress& src, const IpAddress& dst,
                         uint8 protocol, std::string_view data) {
  if (src.family() != dst.family()) return 0;

  uint32 sum = 0;
  uint32 length = static_cast<uint32>(data.size());
  if (src.IsV4()) {
    sum = AccumulateSpan(
        std::span<const uint8>(src.bytes().data(), IpAddress::kV4Length), sum);
    sum = AccumulateSpan(
        std::span<const uint8>(dst.bytes().data(), IpAddress::kV4Length), sum);
    sum += static_cast<uint32>(protocol);
    sum += (length >> kBitsPerWord) + (length & kWordMask);
  } else if (src.IsV6()) {
    sum = AccumulateSpan(
        std::span<const uint8>(src.bytes().data(), IpAddress::kV6Length), sum);
    sum = AccumulateSpan(
        std::span<const uint8>(dst.bytes().data(), IpAddress::kV6Length), sum);
    sum += (length >> kBitsPerWord) + (length & kWordMask);
    sum += static_cast<uint32>(protocol);
  } else {
    return 0;
  }

  return InternetChecksum(data, sum);
}
