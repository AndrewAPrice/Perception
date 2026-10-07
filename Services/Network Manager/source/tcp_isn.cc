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

#include "tcp_isn.h"

#include <vector>

namespace {

// Number of SipHash compression rounds per message block.
constexpr int kCompressionRounds = 2;

// Number of SipHash finalization rounds.
constexpr int kFinalizationRounds = 4;

// Duration of one tick of the RFC 6528 ISN timer M.
constexpr std::chrono::microseconds kIsnTickDuration =
    std::chrono::microseconds(4);

// SipHash initialization constant for v0 ("somepseu").
constexpr uint64 kSipInitV0 = 0x736f6d6570736575ULL;

// SipHash initialization constant for v1 ("dorandom").
constexpr uint64 kSipInitV1 = 0x646f72616e646f6dULL;

// SipHash initialization constant for v2 ("lygenera").
constexpr uint64 kSipInitV2 = 0x6c7967656e657261ULL;

// SipHash initialization constant for v3 ("tedbytes").
constexpr uint64 kSipInitV3 = 0x7465646279746573ULL;

// Value XORed into v2 before finalization.
constexpr uint64 kSipFinalizationXor = 0xFF;

// Rotates `value` left by `bits`.
constexpr uint64 RotateLeft(uint64 value, int bits) {
  return (value << bits) | (value >> (64 - bits));
}

// Reads a little-endian 64-bit value.
uint64 ReadLittleEndian64(const uint8* bytes) {
  uint64 value = 0;
  for (int i = 7; i >= 0; i--) value = (value << 8) | bytes[i];
  return value;
}

// The SipHash internal state.
struct SipState {
  uint64 v0;
  uint64 v1;
  uint64 v2;
  uint64 v3;

  // Applies one SipRound.
  void Round() {
    v0 += v1;
    v1 = RotateLeft(v1, 13);
    v1 ^= v0;
    v0 = RotateLeft(v0, 32);
    v2 += v3;
    v3 = RotateLeft(v3, 16);
    v3 ^= v2;
    v0 += v3;
    v3 = RotateLeft(v3, 21);
    v3 ^= v0;
    v2 += v1;
    v1 = RotateLeft(v1, 17);
    v1 ^= v2;
    v2 = RotateLeft(v2, 32);
  }

  // Absorbs one 64-bit message word.
  void Compress(uint64 word) {
    v3 ^= word;
    for (int i = 0; i < kCompressionRounds; i++) Round();
    v0 ^= word;
  }
};

// Appends `port` in network byte order.
void AppendPort(std::vector<uint8>& message, uint16 port) {
  message.push_back(static_cast<uint8>(port >> 8));
  message.push_back(static_cast<uint8>(port));
}

}  // namespace

uint64 SipHash24(const SipHashKey& key, std::span<const uint8> data) {
  uint64 k0 = ReadLittleEndian64(key.data());
  uint64 k1 = ReadLittleEndian64(key.data() + 8);
  SipState state{.v0 = k0 ^ kSipInitV0,
                 .v1 = k1 ^ kSipInitV1,
                 .v2 = k0 ^ kSipInitV2,
                 .v3 = k1 ^ kSipInitV3};

  size_t full_words = data.size() / 8;
  for (size_t i = 0; i < full_words; i++)
    state.Compress(ReadLittleEndian64(data.data() + i * 8));

  // The final word holds the trailing bytes and the length in its top byte.
  uint64 last = static_cast<uint64>(data.size() & 0xFF) << 56;
  for (size_t i = full_words * 8; i < data.size(); i++)
    last |= static_cast<uint64>(data[i]) << (8 * (i - full_words * 8));
  state.Compress(last);

  state.v2 ^= kSipFinalizationXor;
  for (int i = 0; i < kFinalizationRounds; i++) state.Round();
  return state.v0 ^ state.v1 ^ state.v2 ^ state.v3;
}

TcpIsnGenerator::TcpIsnGenerator(const SipHashKey& secret) : secret_(secret) {}

uint32 TcpIsnGenerator::Generate(std::span<const uint8> local_address,
                                 uint16 local_port,
                                 std::span<const uint8> remote_address,
                                 uint16 remote_port, TcpTime now) const {
  // Each address is length-prefixed so tuples of different families can't
  // produce the same message.
  std::vector<uint8> message;
  message.reserve(2 + local_address.size() + remote_address.size() + 4);
  message.push_back(static_cast<uint8>(local_address.size()));
  message.insert(message.end(), local_address.begin(), local_address.end());
  AppendPort(message, local_port);
  message.push_back(static_cast<uint8>(remote_address.size()));
  message.insert(message.end(), remote_address.begin(), remote_address.end());
  AppendPort(message, remote_port);

  uint32 offset = static_cast<uint32>(SipHash24(secret_, message));
  uint32 timer = static_cast<uint32>(now / kIsnTickDuration);
  return timer + offset;
}
