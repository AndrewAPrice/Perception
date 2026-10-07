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

#include "ipsec.h"

#include <algorithm>
#include <cstring>

#include "ipv6_header.h"
#include "mipv6.h"
#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// IPv4 version nibble.
constexpr uint8 kIpv4Version = 4;

// IPv6 version nibble.
constexpr uint8 kIpv6Version = 6;

// Minimum IPv4 header size in bytes.
constexpr size_t kMinIpv4HeaderSize = 20;

// Offset of the total length field in an IPv4 header.
constexpr size_t kIpv4TotalLengthOffset = 2;

// Offset of the protocol field in an IPv4 header.
constexpr size_t kIpv4ProtocolOffset = 9;

// Offset of the header checksum field in an IPv4 header.
constexpr size_t kIpv4ChecksumOffset = 10;

// Default TTL / Hop Limit for outer tunnel headers.
constexpr uint8 kDefaultTunnelTtl = 64;

// TCP protocol number.
constexpr uint8 kProtocolTcp = 6;

// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;

// Size of the fixed ESP header (4-byte SPI + 4-byte Sequence Number).
constexpr size_t kEspHeaderSize = 8;

// Size of the explicit AEAD Initialization Vector in ESP (RFC 4106 / RFC 7634).
constexpr size_t kEspExplicitIvSize = 8;

// Size of the AEAD Integrity Check Value (ICV / tag) in bytes.
constexpr size_t kEspIcvSize = 16;

// Alignment of the ESP trailer (Pad Length + Next Header) in bytes.
constexpr size_t kEspTrailerAlignment = 4;

// Sliding window size in bits for anti-replay protection.
constexpr uint64 kAntiReplayWindowSize = 64;

// AES S-box table (FIPS 197).
constexpr uint8 kAesSbox[256] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b,
    0xfe, 0xd7, 0xab, 0x76, 0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0,
    0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0, 0xb7, 0xfd, 0x93, 0x26,
    0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2,
    0xeb, 0x27, 0xb2, 0x75, 0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0,
    0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84, 0x53, 0xd1, 0x00, 0xed,
    0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f,
    0x50, 0x3c, 0x9f, 0xa8, 0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5,
    0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2, 0xcd, 0x0c, 0x13, 0xec,
    0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14,
    0xde, 0x5e, 0x0b, 0xdb, 0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c,
    0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79, 0xe7, 0xc8, 0x37, 0x6d,
    0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f,
    0x4b, 0xbd, 0x8b, 0x8a, 0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e,
    0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e, 0xe1, 0xf8, 0x98, 0x11,
    0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f,
    0xb0, 0x54, 0xbb, 0x16};

// AES round constants.
constexpr uint8 kAesRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10,
                                0x20, 0x40, 0x80, 0x1b, 0x36};

// Rotates a 32-bit word left by `bits`.
uint32 Rotl32(uint32 v, int bits) { return (v << bits) | (v >> (32 - bits)); }

// Reads a little-endian 32-bit word from `p`.
uint32 ReadLe32(const uint8* p) {
  return static_cast<uint32>(p[0]) | (static_cast<uint32>(p[1]) << 8) |
         (static_cast<uint32>(p[2]) << 16) | (static_cast<uint32>(p[3]) << 24);
}

// Writes a little-endian 32-bit word to `p`.
void WriteLe32(uint8* p, uint32 v) {
  p[0] = static_cast<uint8>(v & 0xFF);
  p[1] = static_cast<uint8>((v >> 8) & 0xFF);
  p[2] = static_cast<uint8>((v >> 16) & 0xFF);
  p[3] = static_cast<uint8>((v >> 24) & 0xFF);
}

// Performs one ChaCha20 quarter-round (RFC 8439 §2.1).
void ChaCha20QuarterRound(uint32& a, uint32& b, uint32& c, uint32& d) {
  a += b;
  d ^= a;
  d = Rotl32(d, 16);
  c += d;
  b ^= c;
  b = Rotl32(b, 12);
  a += b;
  d ^= a;
  d = Rotl32(d, 8);
  c += d;
  b ^= c;
  b = Rotl32(b, 7);
}

// Generates a 64-byte ChaCha20 keystream block for `(key, counter, nonce)` per
// RFC 8439 §2.3.
std::array<uint8, 64> ChaCha20Block(const std::array<uint8, 32>& key,
                                    uint32 counter,
                                    const std::array<uint8, 12>& nonce) {
  uint32 state[16] = {
      0x61707865,
      0x3320646e,
      0x79622d32,
      0x6b206574,
      ReadLe32(&key[0]),
      ReadLe32(&key[4]),
      ReadLe32(&key[8]),
      ReadLe32(&key[12]),
      ReadLe32(&key[16]),
      ReadLe32(&key[20]),
      ReadLe32(&key[24]),
      ReadLe32(&key[28]),
      counter,
      ReadLe32(&nonce[0]),
      ReadLe32(&nonce[4]),
      ReadLe32(&nonce[8]),
  };
  uint32 working[16];
  std::memcpy(working, state, sizeof(state));

  for (int i = 0; i < 10; i++) {
    ChaCha20QuarterRound(working[0], working[4], working[8], working[12]);
    ChaCha20QuarterRound(working[1], working[5], working[9], working[13]);
    ChaCha20QuarterRound(working[2], working[6], working[10], working[14]);
    ChaCha20QuarterRound(working[3], working[7], working[11], working[15]);
    ChaCha20QuarterRound(working[0], working[5], working[10], working[15]);
    ChaCha20QuarterRound(working[1], working[6], working[11], working[12]);
    ChaCha20QuarterRound(working[2], working[7], working[8], working[13]);
    ChaCha20QuarterRound(working[3], working[4], working[9], working[14]);
  }

  std::array<uint8, 64> out{};
  for (int i = 0; i < 16; i++) WriteLe32(&out[i * 4], working[i] + state[i]);
  return out;
}

// Encrypts or decrypts `input` with ChaCha20 starting at `initial_counter`.
std::string ChaCha20Crypt(const std::array<uint8, 32>& key,
                          uint32 initial_counter,
                          const std::array<uint8, 12>& nonce,
                          std::string_view input) {
  std::string out(input.size(), '\0');
  uint32 counter = initial_counter;
  for (size_t offset = 0; offset < input.size(); offset += 64) {
    auto block = ChaCha20Block(key, counter++, nonce);
    size_t chunk = std::min<size_t>(64, input.size() - offset);
    for (size_t i = 0; i < chunk; i++) {
      out[offset + i] =
          static_cast<char>(static_cast<uint8>(input[offset + i]) ^ block[i]);
    }
  }
  return out;
}

// Computes the 16-byte Poly1305 authenticator tag over `message` using the
// 32-byte one-time key `otk` (RFC 8439 §2.5).
std::array<uint8, 16> Poly1305Mac(const std::array<uint8, 32>& otk,
                                  std::string_view message) {
  uint32 r0 = ReadLe32(&otk[0]) & 0x3ffffff;
  uint32 r1 = (ReadLe32(&otk[3]) >> 2) & 0x3ffff03;
  uint32 r2 = (ReadLe32(&otk[6]) >> 4) & 0x3ffc0ff;
  uint32 r3 = (ReadLe32(&otk[9]) >> 6) & 0x3f03fff;
  uint32 r4 = (ReadLe32(&otk[12]) >> 8) & 0x00fffff;

  uint32 s1 = r1 * 5;
  uint32 s2 = r2 * 5;
  uint32 s3 = r3 * 5;
  uint32 s4 = r4 * 5;

  uint32 h0 = 0;
  uint32 h1 = 0;
  uint32 h2 = 0;
  uint32 h3 = 0;
  uint32 h4 = 0;

  for (size_t offset = 0; offset < message.size(); offset += 16) {
    uint8 block[17] = {};
    size_t chunk = std::min<size_t>(16, message.size() - offset);
    std::memcpy(block, &message[offset], chunk);
    block[chunk] = 1;

    h0 += ReadLe32(&block[0]) & 0x3ffffff;
    h1 += (ReadLe32(&block[3]) >> 2) & 0x3ffffff;
    h2 += (ReadLe32(&block[6]) >> 4) & 0x3ffffff;
    h3 += (ReadLe32(&block[9]) >> 6) & 0x3ffffff;
    h4 += (ReadLe32(&block[12]) >> 8) | (static_cast<uint32>(block[16]) << 24);

    uint64 d0 = static_cast<uint64>(h0) * r0 + static_cast<uint64>(h1) * s4 +
                static_cast<uint64>(h2) * s3 + static_cast<uint64>(h3) * s2 +
                static_cast<uint64>(h4) * s1;
    uint64 d1 = static_cast<uint64>(h0) * r1 + static_cast<uint64>(h1) * r0 +
                static_cast<uint64>(h2) * s4 + static_cast<uint64>(h3) * s3 +
                static_cast<uint64>(h4) * s2;
    uint64 d2 = static_cast<uint64>(h0) * r2 + static_cast<uint64>(h1) * r1 +
                static_cast<uint64>(h2) * r0 + static_cast<uint64>(h3) * s4 +
                static_cast<uint64>(h4) * s3;
    uint64 d3 = static_cast<uint64>(h0) * r3 + static_cast<uint64>(h1) * r2 +
                static_cast<uint64>(h2) * r1 + static_cast<uint64>(h3) * r0 +
                static_cast<uint64>(h4) * s4;
    uint64 d4 = static_cast<uint64>(h0) * r4 + static_cast<uint64>(h1) * r3 +
                static_cast<uint64>(h2) * r2 + static_cast<uint64>(h3) * r1 +
                static_cast<uint64>(h4) * r0;

    uint32 c = static_cast<uint32>(d0 >> 26);
    h0 = static_cast<uint32>(d0) & 0x3ffffff;
    d1 += c;
    c = static_cast<uint32>(d1 >> 26);
    h1 = static_cast<uint32>(d1) & 0x3ffffff;
    d2 += c;
    c = static_cast<uint32>(d2 >> 26);
    h2 = static_cast<uint32>(d2) & 0x3ffffff;
    d3 += c;
    c = static_cast<uint32>(d3 >> 26);
    h3 = static_cast<uint32>(d3) & 0x3ffffff;
    d4 += c;
    c = static_cast<uint32>(d4 >> 26);
    h4 = static_cast<uint32>(d4) & 0x3ffffff;
    h0 += c * 5;
    c = h0 >> 26;
    h0 &= 0x3ffffff;
    h1 += c;
  }

  uint32 c = h1 >> 26;
  h1 &= 0x3ffffff;
  h2 += c;
  c = h2 >> 26;
  h2 &= 0x3ffffff;
  h3 += c;
  c = h3 >> 26;
  h3 &= 0x3ffffff;
  h4 += c;
  c = h4 >> 26;
  h4 &= 0x3ffffff;
  h0 += c * 5;
  c = h0 >> 26;
  h0 &= 0x3ffffff;
  h1 += c;

  uint32 g0 = h0 + 5;
  c = g0 >> 26;
  g0 &= 0x3ffffff;
  uint32 g1 = h1 + c;
  c = g1 >> 26;
  g1 &= 0x3ffffff;
  uint32 g2 = h2 + c;
  c = g2 >> 26;
  g2 &= 0x3ffffff;
  uint32 g3 = h3 + c;
  c = g3 >> 26;
  g3 &= 0x3ffffff;
  uint32 g4 = h4 + c - (1u << 26);

  uint32 mask = (g4 >> 31) - 1;
  g0 &= mask;
  g1 &= mask;
  g2 &= mask;
  g3 &= mask;
  g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0;
  h1 = (h1 & mask) | g1;
  h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3;
  h4 = (h4 & mask) | g4;

  uint64 f0 = ((h0) | (h1 << 26)) + static_cast<uint64>(ReadLe32(&otk[16]));
  uint64 f1 =
      ((h1 >> 6) | (h2 << 20)) + static_cast<uint64>(ReadLe32(&otk[20])) + (f0 >> 32);
  uint64 f2 =
      ((h2 >> 12) | (h3 << 14)) + static_cast<uint64>(ReadLe32(&otk[24])) + (f1 >> 32);
  uint64 f3 =
      ((h3 >> 18) | (h4 << 8)) + static_cast<uint64>(ReadLe32(&otk[28])) + (f2 >> 32);

  std::array<uint8, 16> tag{};
  WriteLe32(&tag[0], static_cast<uint32>(f0));
  WriteLe32(&tag[4], static_cast<uint32>(f1));
  WriteLe32(&tag[8], static_cast<uint32>(f2));
  WriteLe32(&tag[12], static_cast<uint32>(f3));
  return tag;
}

// Computes the RFC 8439 §2.8 AEAD Poly1305 tag over `aad` and `ciphertext`.
std::array<uint8, 16> ComputeChaCha20Poly1305Tag(
    const std::array<uint8, 32>& key, const std::array<uint8, 12>& nonce,
    std::string_view aad, std::string_view ciphertext) {
  auto block0 = ChaCha20Block(key, 0, nonce);
  std::array<uint8, 32> otk{};
  std::copy(block0.begin(), block0.begin() + 32, otk.begin());

  std::string mac_data;
  mac_data.append(aad);
  if (aad.size() % 16 != 0) mac_data.append(16 - (aad.size() % 16), '\0');
  mac_data.append(ciphertext);
  if (ciphertext.size() % 16 != 0)
    mac_data.append(16 - (ciphertext.size() % 16), '\0');

  uint8 lengths[16];
  WriteLe32(&lengths[0], static_cast<uint32>(aad.size() & 0xFFFFFFFF));
  WriteLe32(&lengths[4], static_cast<uint32>((static_cast<uint64>(aad.size()) >> 32)));
  WriteLe32(&lengths[8], static_cast<uint32>(ciphertext.size() & 0xFFFFFFFF));
  WriteLe32(&lengths[12],
            static_cast<uint32>((static_cast<uint64>(ciphertext.size()) >> 32)));
  mac_data.append(reinterpret_cast<const char*>(lengths), 16);
  return Poly1305Mac(otk, mac_data);
}

// Multiplies byte `x` by 2 in GF(2^8) with AES irreducible polynomial 0x11B.
uint8 AesXtime(uint8 x) {
  return static_cast<uint8>((x << 1) ^ ((x & 0x80) ? 0x1B : 0x00));
}

// Expands a 16-byte AES-128 key into 176 bytes of round keys.
std::array<uint8, 176> ExpandAes128Key(const uint8* key) {
  std::array<uint8, 176> round_keys{};
  std::memcpy(round_keys.data(), key, 16);
  for (int i = 4; i < 44; i++) {
    uint8 temp[4] = {round_keys[(i - 1) * 4 + 0], round_keys[(i - 1) * 4 + 1],
                     round_keys[(i - 1) * 4 + 2], round_keys[(i - 1) * 4 + 3]};
    if (i % 4 == 0) {
      uint8 t = temp[0];
      temp[0] = kAesSbox[temp[1]] ^ kAesRcon[(i / 4) - 1];
      temp[1] = kAesSbox[temp[2]];
      temp[2] = kAesSbox[temp[3]];
      temp[3] = kAesSbox[t];
    }
    for (int j = 0; j < 4; j++) {
      round_keys[i * 4 + j] = round_keys[(i - 4) * 4 + j] ^ temp[j];
    }
  }
  return round_keys;
}

// Encrypts a single 16-byte block with AES-128 using `round_keys`.
std::array<uint8, 16> Aes128EncryptBlock(
    const std::array<uint8, 176>& round_keys,
    const std::array<uint8, 16>& in) {
  std::array<uint8, 16> s = in;
  for (int i = 0; i < 16; i++) s[i] ^= round_keys[i];

  for (int round = 1; round <= 10; round++) {
    for (int i = 0; i < 16; i++) s[i] = kAesSbox[s[i]];

    uint8 t = s[1];
    s[1] = s[5];
    s[5] = s[9];
    s[9] = s[13];
    s[13] = t;

    t = s[2];
    s[2] = s[10];
    s[10] = t;
    t = s[6];
    s[6] = s[14];
    s[14] = t;

    t = s[15];
    s[15] = s[11];
    s[11] = s[7];
    s[7] = s[3];
    s[3] = t;

    if (round < 10) {
      for (int c = 0; c < 4; c++) {
        int idx = c * 4;
        uint8 a0 = s[idx + 0];
        uint8 a1 = s[idx + 1];
        uint8 a2 = s[idx + 2];
        uint8 a3 = s[idx + 3];
        uint8 xor_all = a0 ^ a1 ^ a2 ^ a3;
        s[idx + 0] ^= xor_all ^ AesXtime(a0 ^ a1);
        s[idx + 1] ^= xor_all ^ AesXtime(a1 ^ a2);
        s[idx + 2] ^= xor_all ^ AesXtime(a2 ^ a3);
        s[idx + 3] ^= xor_all ^ AesXtime(a3 ^ a0);
      }
    }

    for (int i = 0; i < 16; i++) s[i] ^= round_keys[round * 16 + i];
  }
  return s;
}

// Multiplies two 128-bit blocks `x` and `y` in GF(2^128) for AES-GCM GHASH.
std::array<uint8, 16> GhashMultiply(const std::array<uint8, 16>& x,
                                    const std::array<uint8, 16>& y) {
  std::array<uint8, 16> z{};
  std::array<uint8, 16> v = y;

  for (int i = 0; i < 128; i++) {
    if ((x[i / 8] >> (7 - (i % 8))) & 1) {
      for (int j = 0; j < 16; j++) z[j] ^= v[j];
    }
    uint8 lsb = v[15] & 1;
    for (int j = 15; j > 0; j--) {
      v[j] = static_cast<uint8>((v[j] >> 1) | ((v[j - 1] & 1) << 7));
    }
    v[0] >>= 1;
    if (lsb) v[0] ^= 0xE1;
  }
  return z;
}

// Computes the AES-GCM GHASH over `aad` and `ciphertext` with hash subkey `h`.
std::array<uint8, 16> ComputeGhash(const std::array<uint8, 16>& h,
                                   std::string_view aad,
                                   std::string_view ciphertext) {
  std::array<uint8, 16> y{};
  auto absorb = [&](std::string_view data) {
    for (size_t off = 0; off < data.size(); off += 16) {
      std::array<uint8, 16> block{};
      size_t chunk = std::min<size_t>(16, data.size() - off);
      std::memcpy(block.data(), &data[off], chunk);
      for (int i = 0; i < 16; i++) y[i] ^= block[i];
      y = GhashMultiply(y, h);
    }
  };
  absorb(aad);
  absorb(ciphertext);

  std::array<uint8, 16> len_block{};
  uint64 aad_bits = static_cast<uint64>(aad.size()) * 8;
  uint64 ct_bits = static_cast<uint64>(ciphertext.size()) * 8;
  for (int i = 0; i < 8; i++) {
    len_block[i] = static_cast<uint8>((aad_bits >> ((7 - i) * 8)) & 0xFF);
    len_block[8 + i] = static_cast<uint8>((ct_bits >> ((7 - i) * 8)) & 0xFF);
  }
  for (int i = 0; i < 16; i++) y[i] ^= len_block[i];
  return GhashMultiply(y, h);
}

// Encrypts/decrypts `input` with AES-128-CTR starting at counter `initial_ctr`.
std::string Aes128GcmCtr(const std::array<uint8, 176>& round_keys,
                         const std::array<uint8, 12>& nonce, uint32 initial_ctr,
                         std::string_view input) {
  std::string out(input.size(), '\0');
  uint32 ctr = initial_ctr;
  for (size_t off = 0; off < input.size(); off += 16) {
    std::array<uint8, 16> cb{};
    std::copy(nonce.begin(), nonce.end(), cb.begin());
    cb[12] = static_cast<uint8>((ctr >> 24) & 0xFF);
    cb[13] = static_cast<uint8>((ctr >> 16) & 0xFF);
    cb[14] = static_cast<uint8>((ctr >> 8) & 0xFF);
    cb[15] = static_cast<uint8>(ctr & 0xFF);
    ctr++;
    auto ks = Aes128EncryptBlock(round_keys, cb);
    size_t chunk = std::min<size_t>(16, input.size() - off);
    for (size_t i = 0; i < chunk; i++) {
      out[off + i] =
          static_cast<char>(static_cast<uint8>(input[off + i]) ^ ks[i]);
    }
  }
  return out;
}

// Computes the RFC 1071 ones'-complement checksum over `data`.
uint16 ComputeIpv4Checksum(std::string_view data) {
  uint32 sum = 0;
  size_t i = 0;
  while (i + 1 < data.size()) {
    uint16 word = (static_cast<uint16>(static_cast<uint8>(data[i])) << 8) |
                  static_cast<uint16>(static_cast<uint8>(data[i + 1]));
    sum += word;
    i += 2;
  }
  if (i < data.size())
    sum += static_cast<uint16>(static_cast<uint8>(data[i])) << 8;
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return static_cast<uint16>(~sum);
}

// Builds a 12-byte AEAD nonce from the 4-byte SA salt and 8-byte explicit IV.
std::array<uint8, 12> MakeEspNonce(const std::array<uint8, 4>& salt,
                                   std::string_view explicit_iv) {
  std::array<uint8, 12> nonce{};
  std::copy(salt.begin(), salt.end(), nonce.begin());
  for (size_t i = 0; i < 8 && i < explicit_iv.size(); i++)
    nonce[4 + i] = static_cast<uint8>(explicit_iv[i]);
  return nonce;
}

// Prepends an outer IPv4 or IPv6 header for Tunnel-mode ESP.
std::string PrependTunnelOuterHeader(const IpAddress& local,
                                     const IpAddress& remote,
                                     std::string_view esp_payload) {
  if (local.IsV4()) {
    uint16 total_len =
        static_cast<uint16>(kMinIpv4HeaderSize + esp_payload.size());
    std::string out;
    out.reserve(total_len);
    WireWriter writer(out);
    writer.WriteU8((kIpv4Version << 4) | 5);
    writer.WriteU8(0);
    writer.WriteU16(total_len);
    writer.WriteU16(0);
    writer.WriteU16(0);
    writer.WriteU8(kDefaultTunnelTtl);
    writer.WriteU8(kIpProtocolEsp);
    writer.WriteU16(0);
    for (size_t i = 0; i < IpAddress::kV4Length; i++)
      writer.WriteU8(local.bytes()[i]);
    for (size_t i = 0; i < IpAddress::kV4Length; i++)
      writer.WriteU8(remote.bytes()[i]);
    uint16 csum = ComputeIpv4Checksum(
        std::string_view(out).substr(0, kMinIpv4HeaderSize));
    writer.PatchU16(kIpv4ChecksumOffset, csum);
    writer.WriteBytes(esp_payload);
    return out;
  }

  Ipv6Datagram outer;
  outer.source = local;
  outer.destination = remote;
  outer.next_header = kIpProtocolEsp;
  outer.hop_limit = kDefaultTunnelTtl;
  outer.payload.assign(esp_payload);
  return SerializeIpv6Datagram(outer);
}

}  // namespace

std::string AeadEncrypt(EspCipherSuite cipher,
                        const std::array<uint8, 32>& key,
                        const std::array<uint8, 12>& nonce,
                        std::string_view aad, std::string_view plaintext) {
  if (cipher == EspCipherSuite::ChaCha20Poly1305) {
    std::string ciphertext = ChaCha20Crypt(key, 1, nonce, plaintext);
    auto tag = ComputeChaCha20Poly1305Tag(key, nonce, aad, ciphertext);
    ciphertext.append(reinterpret_cast<const char*>(tag.data()), tag.size());
    return ciphertext;
  }

  auto round_keys = ExpandAes128Key(key.data());
  std::array<uint8, 16> zero_block{};
  auto h = Aes128EncryptBlock(round_keys, zero_block);
  std::string ciphertext = Aes128GcmCtr(round_keys, nonce, 2, plaintext);
  auto ghash = ComputeGhash(h, aad, ciphertext);
  std::array<uint8, 16> j0{};
  std::copy(nonce.begin(), nonce.end(), j0.begin());
  j0[15] = 1;
  auto tag_mask = Aes128EncryptBlock(round_keys, j0);
  for (int i = 0; i < 16; i++) ghash[i] ^= tag_mask[i];
  ciphertext.append(reinterpret_cast<const char*>(ghash.data()), ghash.size());
  return ciphertext;
}

std::optional<std::string> AeadDecrypt(EspCipherSuite cipher,
                                       const std::array<uint8, 32>& key,
                                       const std::array<uint8, 12>& nonce,
                                       std::string_view aad,
                                       std::string_view ciphertext_and_tag) {
  if (ciphertext_and_tag.size() < kEspIcvSize) return std::nullopt;
  size_t ct_len = ciphertext_and_tag.size() - kEspIcvSize;
  std::string_view ciphertext = ciphertext_and_tag.substr(0, ct_len);
  std::string_view received_tag =
      ciphertext_and_tag.substr(ct_len, kEspIcvSize);

  if (cipher == EspCipherSuite::ChaCha20Poly1305) {
    auto expected_tag = ComputeChaCha20Poly1305Tag(key, nonce, aad, ciphertext);
    uint8 diff = 0;
    for (size_t i = 0; i < kEspIcvSize; i++)
      diff |= (static_cast<uint8>(received_tag[i]) ^ expected_tag[i]);
    if (diff != 0) return std::nullopt;
    return ChaCha20Crypt(key, 1, nonce, ciphertext);
  }

  auto round_keys = ExpandAes128Key(key.data());
  std::array<uint8, 16> zero_block{};
  auto h = Aes128EncryptBlock(round_keys, zero_block);
  auto expected_tag = ComputeGhash(h, aad, ciphertext);
  std::array<uint8, 16> j0{};
  std::copy(nonce.begin(), nonce.end(), j0.begin());
  j0[15] = 1;
  auto tag_mask = Aes128EncryptBlock(round_keys, j0);
  uint8 diff = 0;
  for (int i = 0; i < 16; i++) {
    expected_tag[i] ^= tag_mask[i];
    diff |= (static_cast<uint8>(received_tag[i]) ^ expected_tag[i]);
  }
  if (diff != 0) return std::nullopt;
  return Aes128GcmCtr(round_keys, nonce, 2, ciphertext);
}

std::optional<PacketSelectorTuple> ExtractPacketSelector(
    std::string_view ip_packet) {
  if (ip_packet.empty()) return std::nullopt;
  uint8 version = static_cast<uint8>(ip_packet[0]) >> 4;
  PacketSelectorTuple tuple;

  std::string_view transport_payload;
  if (version == kIpv4Version) {
    if (ip_packet.size() < kMinIpv4HeaderSize) return std::nullopt;
    size_t ihl = static_cast<size_t>(ip_packet[0] & 0x0F) * 4;
    if (ihl < kMinIpv4HeaderSize || ip_packet.size() < ihl) return std::nullopt;
    WireReader reader(ip_packet);
    reader.Skip(2);
    uint16 total_len = reader.ReadU16();
    if (total_len < ihl || ip_packet.size() < total_len) return std::nullopt;
    reader.Skip(5);
    tuple.protocol = reader.ReadU8();
    reader.Skip(2);
    auto src_b = reader.ReadBytes(IpAddress::kV4Length);
    auto dst_b = reader.ReadBytes(IpAddress::kV4Length);
    tuple.family = IpAddressFamily::V4;
    tuple.source = IpAddress::FromBytes(
        IpAddressFamily::V4,
        {reinterpret_cast<const uint8*>(src_b.data()), src_b.size()});
    tuple.destination = IpAddress::FromBytes(
        IpAddressFamily::V4,
        {reinterpret_cast<const uint8*>(dst_b.data()), dst_b.size()});
    transport_payload = ip_packet.substr(ihl, total_len - ihl);
  } else if (version == kIpv6Version) {
    auto hdr = ParseIpv6Header(ip_packet);
    if (!hdr.has_value()) return std::nullopt;
    tuple.family = IpAddressFamily::V6;
    tuple.source = hdr->source;
    tuple.destination = hdr->destination;
    tuple.protocol = hdr->next_header;
    transport_payload = ip_packet.substr(kIpv6HeaderSize, hdr->payload_length);
  } else {
    return std::nullopt;
  }

  if ((tuple.protocol == kProtocolTcp || tuple.protocol == kProtocolUdp) &&
      transport_payload.size() >= 4) {
    WireReader port_reader(transport_payload);
    tuple.source_port = port_reader.ReadU16();
    tuple.destination_port = port_reader.ReadU16();
  }
  return tuple;
}

bool AntiReplayWindow::Check(uint64 sequence_number) const {
  if (sequence_number == 0) return false;
  if (sequence_number > highest_seq_) return true;
  uint64 diff = highest_seq_ - sequence_number;
  if (diff >= kAntiReplayWindowSize) return false;
  return ((bitmap_ >> diff) & 1ULL) == 0;
}

void AntiReplayWindow::Advance(uint64 sequence_number) {
  if (sequence_number == 0) return;
  if (sequence_number > highest_seq_) {
    uint64 shift = sequence_number - highest_seq_;
    if (shift >= kAntiReplayWindowSize) {
      bitmap_ = 1ULL;
    } else {
      bitmap_ = (bitmap_ << shift) | 1ULL;
    }
    highest_seq_ = sequence_number;
  } else {
    uint64 diff = highest_seq_ - sequence_number;
    if (diff < kAntiReplayWindowSize) bitmap_ |= (1ULL << diff);
  }
}

void IpsecEngine::AddSpdRule(const SpdRule& rule) { spd_.push_back(rule); }

void IpsecEngine::InstallSa(const SecurityAssociation& sa) {
  sad_[sa.spi] = sa;
}

bool IpsecEngine::RemoveSa(uint32 spi) { return sad_.erase(spi) > 0; }

const SecurityAssociation* IpsecEngine::FindSa(uint32 spi) const {
  auto it = sad_.find(spi);
  return (it != sad_.end()) ? &it->second : nullptr;
}

SecurityAssociation* IpsecEngine::FindSaMutable(uint32 spi) {
  auto it = sad_.find(spi);
  return (it != sad_.end()) ? &it->second : nullptr;
}

bool IpsecEngine::UpdateTunnelEndpoints(const uint32 spi,
                                        const IpAddress& new_local,
                                        const IpAddress& new_remote) {
  SecurityAssociation* sa = FindSaMutable(spi);
  if (sa == nullptr) return false;
  sa->tunnel_local = new_local;
  sa->tunnel_remote = new_remote;
  return true;
}

const SpdRule* IpsecEngine::MatchSpd(const PacketSelectorTuple& tuple) const {
  for (const SpdRule& rule : spd_) {
    if (rule.family != IpAddressFamily::Unspecified &&
        rule.family != tuple.family)
      continue;
    if (rule.src_prefix_length > 0 &&
        !tuple.source.IsInPrefix(rule.src_prefix, rule.src_prefix_length))
      continue;
    if (rule.dst_prefix_length > 0 &&
        !tuple.destination.IsInPrefix(rule.dst_prefix, rule.dst_prefix_length))
      continue;
    if (rule.protocol != 0 && rule.protocol != tuple.protocol) continue;
    if (tuple.source_port < rule.src_port_min ||
        tuple.source_port > rule.src_port_max)
      continue;
    if (tuple.destination_port < rule.dst_port_min ||
        tuple.destination_port > rule.dst_port_max)
      continue;
    return &rule;
  }
  return nullptr;
}

std::optional<std::string> IpsecEngine::ProtectWithSa(
    std::string_view ip_packet, SecurityAssociation& sa) {
  if (ip_packet.empty()) return std::nullopt;
  uint8 version = static_cast<uint8>(ip_packet[0]) >> 4;

  uint32 seq = sa.next_tx_seq++;
  std::string esp_aad;
  esp_aad.reserve(kEspHeaderSize);
  WireWriter aad_writer(esp_aad);
  aad_writer.WriteU32(sa.spi);
  aad_writer.WriteU32(seq);

  std::string explicit_iv;
  explicit_iv.reserve(kEspExplicitIvSize);
  WireWriter iv_writer(explicit_iv);
  iv_writer.WriteU32(0);
  iv_writer.WriteU32(seq);
  auto nonce = MakeEspNonce(sa.salt, explicit_iv);

  auto build_esp_payload = [&](std::string_view cleartext,
                               uint8 inner_next_header) {
    // Pad `(cleartext.size() + 2)` to a multiple of `kEspTrailerAlignment`.
    size_t rem = (cleartext.size() + 2) % kEspTrailerAlignment;
    size_t pad_len = (rem == 0) ? 0 : (kEspTrailerAlignment - rem);
    std::string esp_plaintext;
    esp_plaintext.reserve(cleartext.size() + pad_len + 2);
    esp_plaintext.append(cleartext);
    for (size_t i = 1; i <= pad_len; i++)
      esp_plaintext.push_back(static_cast<char>(i));
    esp_plaintext.push_back(static_cast<char>(pad_len));
    esp_plaintext.push_back(static_cast<char>(inner_next_header));

    std::string ct_and_tag =
        AeadEncrypt(sa.cipher, sa.key, nonce, esp_aad, esp_plaintext);
    std::string esp;
    esp.reserve(kEspHeaderSize + kEspExplicitIvSize + ct_and_tag.size());
    esp.append(esp_aad);
    esp.append(explicit_iv);
    esp.append(ct_and_tag);
    return esp;
  };

  if (sa.mode == IpsecMode::Tunnel) {
    uint8 inner_proto =
        (version == kIpv4Version) ? kIpProtocolIpv4Encap : kIpProtocolIpv6Encap;
    std::string esp = build_esp_payload(ip_packet, inner_proto);
    return PrependTunnelOuterHeader(sa.tunnel_local, sa.tunnel_remote, esp);
  }

  // Transport mode.
  if (version == kIpv4Version) {
    size_t ihl = static_cast<size_t>(ip_packet[0] & 0x0F) * 4;
    if (ip_packet.size() < ihl) return std::nullopt;
    uint8 orig_proto = static_cast<uint8>(ip_packet[kIpv4ProtocolOffset]);
    std::string esp = build_esp_payload(ip_packet.substr(ihl), orig_proto);

    std::string out;
    uint16 new_total_len = static_cast<uint16>(ihl + esp.size());
    out.reserve(new_total_len);
    out.append(ip_packet.substr(0, ihl));
    out[kIpv4TotalLengthOffset] = static_cast<char>(new_total_len >> 8);
    out[kIpv4TotalLengthOffset + 1] = static_cast<char>(new_total_len & 0xFF);
    out[kIpv4ProtocolOffset] = static_cast<char>(kIpProtocolEsp);
    out[kIpv4ChecksumOffset] = 0;
    out[kIpv4ChecksumOffset + 1] = 0;
    uint16 csum = ComputeIpv4Checksum(std::string_view(out).substr(0, ihl));
    out[kIpv4ChecksumOffset] = static_cast<char>(csum >> 8);
    out[kIpv4ChecksumOffset + 1] = static_cast<char>(csum & 0xFF);
    out.append(esp);
    return out;
  }

  auto hdr = ParseIpv6Header(ip_packet);
  if (!hdr.has_value()) return std::nullopt;
  std::string esp = build_esp_payload(
      ip_packet.substr(kIpv6HeaderSize, hdr->payload_length), hdr->next_header);
  Ipv6Datagram out_dgram;
  out_dgram.source = hdr->source;
  out_dgram.destination = hdr->destination;
  out_dgram.hop_limit = hdr->hop_limit;
  out_dgram.next_header = kIpProtocolEsp;
  out_dgram.payload = std::move(esp);
  return SerializeIpv6Datagram(out_dgram);
}

IpsecResult IpsecEngine::ProcessOutbound(std::string_view ip_packet) {
  IpsecResult res;
  auto tuple = ExtractPacketSelector(ip_packet);
  if (!tuple.has_value()) return res;

  const SpdRule* rule = MatchSpd(*tuple);
  if (rule == nullptr || rule->action == SpdAction::Bypass) {
    res.status = IpsecStatus::Bypassed;
    res.packet.assign(ip_packet);
    return res;
  }
  if (rule->action == SpdAction::Discard) return res;

  SecurityAssociation* sa = FindSaMutable(rule->sa_spi);
  if (sa == nullptr) return res;

  auto protected_pkt = ProtectWithSa(ip_packet, *sa);
  if (!protected_pkt.has_value()) return res;
  res.status = IpsecStatus::Protected;
  res.spi = sa->spi;
  res.packet = std::move(*protected_pkt);
  return res;
}

IpsecResult IpsecEngine::ProcessInbound(std::string_view ip_packet) {
  IpsecResult res;
  if (ip_packet.empty()) return res;

  uint8 version = static_cast<uint8>(ip_packet[0]) >> 4;
  uint8 outer_proto = 0;
  size_t esp_offset = 0;
  size_t esp_len = 0;

  if (version == kIpv4Version) {
    if (ip_packet.size() < kMinIpv4HeaderSize) return res;
    size_t ihl = static_cast<size_t>(ip_packet[0] & 0x0F) * 4;
    WireReader r(ip_packet);
    r.Skip(2);
    uint16 total_len = r.ReadU16();
    if (ihl < kMinIpv4HeaderSize || total_len < ihl ||
        ip_packet.size() < total_len)
      return res;
    outer_proto = static_cast<uint8>(ip_packet[kIpv4ProtocolOffset]);
    esp_offset = ihl;
    esp_len = total_len - ihl;
  } else if (version == kIpv6Version) {
    auto hdr = ParseIpv6Header(ip_packet);
    if (!hdr.has_value()) return res;
    outer_proto = hdr->next_header;
    esp_offset = kIpv6HeaderSize;
    esp_len = hdr->payload_length;
  } else {
    return res;
  }

  if (outer_proto != kIpProtocolEsp) {
    auto tuple = ExtractPacketSelector(ip_packet);
    if (!tuple.has_value()) return res;
    const SpdRule* rule = MatchSpd(*tuple);
    if (rule != nullptr && rule->action != SpdAction::Bypass) return res;
    res.status = IpsecStatus::Bypassed;
    res.packet.assign(ip_packet);
    return res;
  }

  if (esp_len < kEspHeaderSize + kEspExplicitIvSize + kEspIcvSize + 2)
    return res;
  std::string_view esp_bytes = ip_packet.substr(esp_offset, esp_len);
  WireReader esp_reader(esp_bytes);
  uint32 spi = esp_reader.ReadU32();
  uint32 seq = esp_reader.ReadU32();
  std::string_view explicit_iv = esp_reader.ReadBytes(kEspExplicitIvSize);
  std::string_view ct_and_tag = esp_reader.Rest();

  SecurityAssociation* sa = FindSaMutable(spi);
  if (sa == nullptr || !sa->replay_window.Check(seq)) return res;

  auto nonce = MakeEspNonce(sa->salt, explicit_iv);
  std::string_view aad = esp_bytes.substr(0, kEspHeaderSize);
  auto decrypted = AeadDecrypt(sa->cipher, sa->key, nonce, aad, ct_and_tag);
  if (!decrypted.has_value() || decrypted->size() < 2) return res;

  uint8 next_header = static_cast<uint8>((*decrypted)[decrypted->size() - 1]);
  uint8 pad_len = static_cast<uint8>((*decrypted)[decrypted->size() - 2]);
  if (static_cast<size_t>(pad_len) + 2 > decrypted->size()) return res;

  // Verify RFC 4303 §2.4 monotonic padding bytes 1, 2, ..., pad_len.
  size_t payload_len = decrypted->size() - 2 - pad_len;
  for (size_t i = 0; i < pad_len; i++) {
    if (static_cast<uint8>((*decrypted)[payload_len + i]) != i + 1) return res;
  }

  // Only advance the anti-replay window after AEAD authentication and trailer
  // validation succeed (RFC 4303 §3.4.3).
  sa->replay_window.Advance(seq);
  std::string_view cleartext = std::string_view(*decrypted).substr(0, payload_len);

  if (sa->mode == IpsecMode::Tunnel) {
    if (next_header != kIpProtocolIpv4Encap &&
        next_header != kIpProtocolIpv6Encap)
      return res;
    res.status = IpsecStatus::Protected;
    res.spi = spi;
    res.packet.assign(cleartext);
    return res;
  }

  // Transport mode reconstruction.
  if (version == kIpv4Version) {
    std::string out;
    uint16 new_total_len = static_cast<uint16>(esp_offset + cleartext.size());
    out.reserve(new_total_len);
    out.append(ip_packet.substr(0, esp_offset));
    out[kIpv4TotalLengthOffset] = static_cast<char>(new_total_len >> 8);
    out[kIpv4TotalLengthOffset + 1] = static_cast<char>(new_total_len & 0xFF);
    out[kIpv4ProtocolOffset] = static_cast<char>(next_header);
    out[kIpv4ChecksumOffset] = 0;
    out[kIpv4ChecksumOffset + 1] = 0;
    uint16 csum =
        ComputeIpv4Checksum(std::string_view(out).substr(0, esp_offset));
    out[kIpv4ChecksumOffset] = static_cast<char>(csum >> 8);
    out[kIpv4ChecksumOffset + 1] = static_cast<char>(csum & 0xFF);
    out.append(cleartext);
    res.status = IpsecStatus::Protected;
    res.spi = spi;
    res.packet = std::move(out);
    return res;
  }

  auto hdr = ParseIpv6Header(ip_packet);
  Ipv6Datagram dgram;
  dgram.source = hdr->source;
  dgram.destination = hdr->destination;
  dgram.hop_limit = hdr->hop_limit;
  dgram.next_header = next_header;
  dgram.payload.assign(cleartext);
  res.status = IpsecStatus::Protected;
  res.spi = spi;
  res.packet = SerializeIpv6Datagram(dgram);
  return res;
}
