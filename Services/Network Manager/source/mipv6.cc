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

#include "mipv6.h"

#include <algorithm>
#include <cstring>

#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Minimum Mobility Header length in bytes (8-byte fixed header).
constexpr size_t kMinMobilityHeaderSize = 8;

// Alignment unit of Mobility Header and IPv6 extension headers in bytes.
constexpr size_t kHeaderAlignmentUnit = 8;

// Default Hop Limit for Mobility Header and tunneled packets.
constexpr uint8 kDefaultMip6HopLimit = 64;

// Mobility Option Type: Pad1 (RFC 6275 §6.2.1).
constexpr uint8 kMobOptPad1 = 0;

// Mobility Option Type: PadN (RFC 6275 §6.2.1).
constexpr uint8 kMobOptPadN = 1;

// Mobility Option Type: Binding Refresh Advice (RFC 6275 §6.2.2).
constexpr uint8 kMobOptRefreshAdvice = 2;

// Mobility Option Type: Alternate Care-of Address (RFC 6275 §6.2.3).
constexpr uint8 kMobOptAlternateCoa = 3;

// Mobility Option Type: Nonce Indices (RFC 6275 §6.2.4).
constexpr uint8 kMobOptNonceIndices = 4;

// Mobility Option Type: Binding Authorization Data (RFC 6275 §6.2.5).
constexpr uint8 kMobOptAuthData = 5;

// Binding Update flag: Acknowledge (A).
constexpr uint8 kBuFlagAcknowledge = 0x80;

// Binding Update flag: Home Registration (H).
constexpr uint8 kBuFlagHomeRegistration = 0x40;

// Binding Update flag: Link-Local Address Compatibility (L).
constexpr uint8 kBuFlagLinkLocal = 0x20;

// Binding Update flag: Key Management Mobility Capability (K).
constexpr uint8 kBuFlagKeyManagement = 0x10;

// Binding Acknowledgement flag: Key Management Mobility Capability (K).
constexpr uint8 kBaFlagKeyManagement = 0x80;

// Seconds per lifetime unit in Binding Update / Acknowledgement (RFC 6275).
constexpr uint32 kLifetimeUnitSeconds = 4;

// Size of a Type 2 Routing Header in bytes.
constexpr size_t kType2RoutingHeaderSize = 24;

// Hdr Ext Len value for a 24-byte extension header ((24 / 8) - 1 = 2).
constexpr uint8 kHdrExtLen24Bytes = 2;

// Size of the SHA-1 digest in bytes.
constexpr size_t kSha1DigestSize = 20;

// Block size of SHA-1 in bytes.
constexpr size_t kSha1BlockSize = 64;

// Size of the Binding Authorization Data authenticator in bytes (96 bits).
constexpr size_t kAuthenticatorSize = 12;

// Left-rotates a 32-bit word by `bits`.
uint32 RotateLeft32(uint32 value, int bits) {
  return (value << bits) | (value >> (32 - bits));
}

// Self-contained RFC 3174 SHA-1 implementation for RFC 6275 return routability
// keygen token and K_bm authenticator calculation.
std::array<uint8, kSha1DigestSize> ComputeSha1(std::string_view data) {
  uint32 h0 = 0x67452301;
  uint32 h1 = 0xEFCDAB89;
  uint32 h2 = 0x98BADCFE;
  uint32 h3 = 0x10325476;
  uint32 h4 = 0xC3D2E1F0;

  std::string padded(data);
  padded.push_back(static_cast<char>(0x80));
  while ((padded.size() % kSha1BlockSize) != 56) padded.push_back('\0');

  uint64 bit_len = static_cast<uint64>(data.size()) * 8;
  for (int i = 7; i >= 0; i--) {
    padded.push_back(static_cast<char>((bit_len >> (i * 8)) & 0xFF));
  }

  for (size_t chunk = 0; chunk < padded.size(); chunk += kSha1BlockSize) {
    uint32 w[80];
    for (int i = 0; i < 16; i++) {
      size_t off = chunk + i * 4;
      w[i] = (static_cast<uint32>(static_cast<uint8>(padded[off])) << 24) |
             (static_cast<uint32>(static_cast<uint8>(padded[off + 1])) << 16) |
             (static_cast<uint32>(static_cast<uint8>(padded[off + 2])) << 8) |
             static_cast<uint32>(static_cast<uint8>(padded[off + 3]));
    }
    for (int i = 16; i < 80; i++) {
      w[i] = RotateLeft32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32 a = h0;
    uint32 b = h1;
    uint32 c = h2;
    uint32 d = h3;
    uint32 e = h4;

    for (int i = 0; i < 80; i++) {
      uint32 f = 0;
      uint32 k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDC;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6;
      }
      uint32 temp = RotateLeft32(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = RotateLeft32(b, 30);
      b = a;
      a = temp;
    }

    h0 += a;
    h1 += b;
    h2 += c;
    h3 += d;
    h4 += e;
  }

  std::array<uint8, kSha1DigestSize> digest{};
  uint32 words[5] = {h0, h1, h2, h3, h4};
  for (int i = 0; i < 5; i++) {
    digest[i * 4] = static_cast<uint8>((words[i] >> 24) & 0xFF);
    digest[i * 4 + 1] = static_cast<uint8>((words[i] >> 16) & 0xFF);
    digest[i * 4 + 2] = static_cast<uint8>((words[i] >> 8) & 0xFF);
    digest[i * 4 + 3] = static_cast<uint8>(words[i] & 0xFF);
  }
  return digest;
}

// Computes HMAC-SHA1(key, message) per RFC 2104.
std::array<uint8, kSha1DigestSize> ComputeHmacSha1(std::string_view key,
                                                   std::string_view message) {
  std::array<uint8, kSha1BlockSize> key_block{};
  if (key.size() > kSha1BlockSize) {
    auto hashed_key = ComputeSha1(key);
    std::copy(hashed_key.begin(), hashed_key.end(), key_block.begin());
  } else {
    for (size_t i = 0; i < key.size(); i++)
      key_block[i] = static_cast<uint8>(key[i]);
  }

  std::string inner_input;
  inner_input.reserve(kSha1BlockSize + message.size());
  for (uint8 b : key_block) inner_input.push_back(static_cast<char>(b ^ 0x36));
  inner_input.append(message);
  auto inner_digest = ComputeSha1(inner_input);

  std::string outer_input;
  outer_input.reserve(kSha1BlockSize + kSha1DigestSize);
  for (uint8 b : key_block) outer_input.push_back(static_cast<char>(b ^ 0x5C));
  outer_input.append(reinterpret_cast<const char*>(inner_digest.data()),
                     inner_digest.size());
  return ComputeSha1(outer_input);
}

// Computes the RFC 1071 ones'-complement checksum over `data`.
uint16 OnesComplementChecksum(std::string_view data) {
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

// Computes the Mobility Header checksum over the IPv6 pseudo-header (Next
// Header 135) and `mh_payload` (RFC 6275 §6.1.1).
uint16 ComputeMobilityChecksum(const IpAddress& source,
                               const IpAddress& destination,
                               std::string_view mh_payload) {
  std::string buf;
  buf.reserve(kIpv6HeaderSize + mh_payload.size());
  WireWriter writer(buf);
  writer.WriteIpv6Address(source);
  writer.WriteIpv6Address(destination);
  writer.WriteU32(static_cast<uint32>(mh_payload.size()));
  writer.WriteZeros(3);
  writer.WriteU8(kIpProtocolMobility);
  writer.WriteBytes(mh_payload);
  return OnesComplementChecksum(buf);
}

// Appends `Pad1` / `PadN` mobility options to align `buf.size()` to `align * n + rem`.
void PadMobilityOptions(std::string& buf, size_t align, size_t rem) {
  size_t current_rem = buf.size() % align;
  size_t needed = (rem + align - current_rem) % align;
  if (needed == 0) return;
  if (needed == 1) {
    buf.push_back(static_cast<char>(kMobOptPad1));
    return;
  }
  buf.push_back(static_cast<char>(kMobOptPadN));
  buf.push_back(static_cast<char>(needed - 2));
  buf.append(needed - 2, '\0');
}

// Computes the 12-byte (96-bit) Binding Authorization Data authenticator over
// `mh_with_zeroed_checksum_and_auth` per RFC 6275 §6.2.5.
std::array<uint8, kAuthenticatorSize> ComputeBindingAuthenticator(
    const std::array<uint8, kSha1DigestSize>& k_bm,
    const IpAddress& care_of_address, const IpAddress& cn_address,
    std::string_view mh_with_zeroed_checksum_and_auth) {
  std::string auth_input;
  auth_input.reserve(32 + mh_with_zeroed_checksum_and_auth.size());
  WireWriter writer(auth_input);
  writer.WriteIpv6Address(care_of_address);
  writer.WriteIpv6Address(cn_address);
  writer.WriteBytes(mh_with_zeroed_checksum_and_auth);

  std::string_view key_view(reinterpret_cast<const char*>(k_bm.data()),
                            k_bm.size());
  auto hmac = ComputeHmacSha1(key_view, auth_input);
  std::array<uint8, kAuthenticatorSize> out{};
  std::copy(hmac.begin(), hmac.begin() + kAuthenticatorSize, out.begin());
  return out;
}

// Returns true if 16-bit sequence number `a` is greater than `b` in RFC 6275
// modulo 2^16 serial number arithmetic.
bool SequenceGreaterOrEqual(uint16 a, uint16 b) {
  return static_cast<int16>(a - b) >= 0;
}

}  // namespace

std::array<uint8, 20> DeriveBindingManagementKey(
    const std::array<uint8, 8>& home_keygen_token,
    const std::array<uint8, 8>& care_of_keygen_token) {
  std::string tokens;
  tokens.reserve(16);
  tokens.append(reinterpret_cast<const char*>(home_keygen_token.data()), 8);
  tokens.append(reinterpret_cast<const char*>(care_of_keygen_token.data()), 8);
  return ComputeSha1(tokens);
}

std::string SerializeMobilityHeader(
    const IpAddress& checksum_source, const IpAddress& checksum_destination,
    const MobilityMessage& message,
    const std::optional<std::array<uint8, 20>>& k_bm,
    const IpAddress& auth_care_of_address, const IpAddress& auth_cn_address) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::NoNextHeader));
  writer.WriteU8(0);
  writer.WriteU8(static_cast<uint8>(message.type));
  writer.WriteU8(0);
  writer.WriteU16(0);

  switch (message.type) {
    case MobilityHeaderType::BindingRefreshRequest:
      writer.WriteU16(0);
      break;
    case MobilityHeaderType::HomeTestInit:
    case MobilityHeaderType::CareOfTestInit:
      writer.WriteU16(0);
      writer.WriteBytes(
          {reinterpret_cast<const char*>(message.init_cookie.data()), 8});
      break;
    case MobilityHeaderType::HomeTest:
    case MobilityHeaderType::CareOfTest:
      writer.WriteU16(message.nonce_index);
      writer.WriteBytes(
          {reinterpret_cast<const char*>(message.init_cookie.data()), 8});
      writer.WriteBytes(
          {reinterpret_cast<const char*>(message.keygen_token.data()), 8});
      break;
    case MobilityHeaderType::BindingUpdate: {
      writer.WriteU16(message.sequence_number);
      uint8 flags = 0;
      if (message.acknowledge_requested) flags |= kBuFlagAcknowledge;
      if (message.home_registration) flags |= kBuFlagHomeRegistration;
      if (message.link_local_compatibility) flags |= kBuFlagLinkLocal;
      if (message.key_management_mobility) flags |= kBuFlagKeyManagement;
      writer.WriteU8(flags);
      writer.WriteU8(0);
      writer.WriteU16(message.lifetime_units);
      break;
    }
    case MobilityHeaderType::BindingAcknowledgement: {
      writer.WriteU8(message.status);
      uint8 flags = message.key_management_mobility ? kBaFlagKeyManagement : 0;
      writer.WriteU8(flags);
      writer.WriteU16(message.sequence_number);
      writer.WriteU16(message.lifetime_units);
      break;
    }
    case MobilityHeaderType::BindingError:
      writer.WriteU8(message.status);
      writer.WriteU8(0);
      writer.WriteIpv6Address(message.error_home_address);
      break;
  }

  if (message.alternate_care_of_address.has_value()) {
    // Alternate Care-of Address option requires 8n + 6 alignment.
    PadMobilityOptions(out, 8, 6);
    writer.WriteU8(kMobOptAlternateCoa);
    writer.WriteU8(16);
    writer.WriteIpv6Address(*message.alternate_care_of_address);
  }

  if (message.nonce_indices.has_value()) {
    // Nonce Indices option requires 2n alignment.
    PadMobilityOptions(out, 2, 0);
    writer.WriteU8(kMobOptNonceIndices);
    writer.WriteU8(4);
    writer.WriteU16(message.nonce_indices->first);
    writer.WriteU16(message.nonce_indices->second);
  }

  if (message.refresh_advice_units.has_value()) {
    PadMobilityOptions(out, 2, 0);
    writer.WriteU8(kMobOptRefreshAdvice);
    writer.WriteU8(2);
    writer.WriteU16(*message.refresh_advice_units);
  }

  std::optional<size_t> auth_data_offset;
  if (message.authenticator.has_value() || k_bm.has_value()) {
    writer.WriteU8(kMobOptAuthData);
    writer.WriteU8(kAuthenticatorSize);
    auth_data_offset = out.size();
    if (message.authenticator.has_value() && !k_bm.has_value()) {
      writer.WriteBytes({reinterpret_cast<const char*>(
                             message.authenticator->data()),
                         kAuthenticatorSize});
    } else {
      writer.WriteZeros(kAuthenticatorSize);
    }
  }

  PadMobilityOptions(out, kHeaderAlignmentUnit, 0);
  uint8 header_len =
      static_cast<uint8>((out.size() / kHeaderAlignmentUnit) - 1);
  out[1] = static_cast<char>(header_len);

  if (k_bm.has_value() && auth_data_offset.has_value()) {
    IpAddress coa = auth_care_of_address.IsUnspecified()
                        ? checksum_source
                        : auth_care_of_address;
    IpAddress cn = auth_cn_address.IsUnspecified() ? checksum_destination
                                                   : auth_cn_address;
    auto auth = ComputeBindingAuthenticator(*k_bm, coa, cn, out);
    std::memcpy(&out[*auth_data_offset], auth.data(), kAuthenticatorSize);
  }

  uint16 checksum =
      ComputeMobilityChecksum(checksum_source, checksum_destination, out);
  writer.PatchU16(4, checksum);
  return out;
}

std::optional<MobilityMessage> ParseMobilityHeader(
    const IpAddress& checksum_source, const IpAddress& checksum_destination,
    std::string_view payload) {
  if (payload.size() < kMinMobilityHeaderSize) return std::nullopt;
  uint8 payload_proto = static_cast<uint8>(payload[0]);
  if (payload_proto != static_cast<uint8>(Ipv6NextHeader::NoNextHeader))
    return std::nullopt;

  size_t total_len =
      (static_cast<size_t>(static_cast<uint8>(payload[1])) + 1) *
      kHeaderAlignmentUnit;
  if (payload.size() < total_len) return std::nullopt;
  payload = payload.substr(0, total_len);

  if (ComputeMobilityChecksum(checksum_source, checksum_destination, payload) !=
      0)
    return std::nullopt;

  WireReader reader(payload);
  reader.Skip(2);
  uint8 mh_type_raw = reader.ReadU8();
  if (mh_type_raw > static_cast<uint8>(MobilityHeaderType::BindingError))
    return std::nullopt;
  reader.Skip(3);

  MobilityMessage msg;
  msg.type = static_cast<MobilityHeaderType>(mh_type_raw);

  switch (msg.type) {
    case MobilityHeaderType::BindingRefreshRequest:
      reader.Skip(2);
      break;
    case MobilityHeaderType::HomeTestInit:
    case MobilityHeaderType::CareOfTestInit: {
      reader.Skip(2);
      std::string_view cookie = reader.ReadBytes(8);
      if (!reader.ok()) return std::nullopt;
      std::memcpy(msg.init_cookie.data(), cookie.data(), 8);
      break;
    }
    case MobilityHeaderType::HomeTest:
    case MobilityHeaderType::CareOfTest: {
      msg.nonce_index = reader.ReadU16();
      std::string_view cookie = reader.ReadBytes(8);
      std::string_view token = reader.ReadBytes(8);
      if (!reader.ok()) return std::nullopt;
      std::memcpy(msg.init_cookie.data(), cookie.data(), 8);
      std::memcpy(msg.keygen_token.data(), token.data(), 8);
      break;
    }
    case MobilityHeaderType::BindingUpdate: {
      msg.sequence_number = reader.ReadU16();
      uint8 flags = reader.ReadU8();
      msg.acknowledge_requested = (flags & kBuFlagAcknowledge) != 0;
      msg.home_registration = (flags & kBuFlagHomeRegistration) != 0;
      msg.link_local_compatibility = (flags & kBuFlagLinkLocal) != 0;
      msg.key_management_mobility = (flags & kBuFlagKeyManagement) != 0;
      reader.Skip(1);
      msg.lifetime_units = reader.ReadU16();
      if (!reader.ok()) return std::nullopt;
      break;
    }
    case MobilityHeaderType::BindingAcknowledgement: {
      msg.status = reader.ReadU8();
      uint8 flags = reader.ReadU8();
      msg.key_management_mobility = (flags & kBaFlagKeyManagement) != 0;
      msg.sequence_number = reader.ReadU16();
      msg.lifetime_units = reader.ReadU16();
      if (!reader.ok()) return std::nullopt;
      break;
    }
    case MobilityHeaderType::BindingError:
      msg.status = reader.ReadU8();
      reader.Skip(1);
      msg.error_home_address = reader.ReadIpv6Address();
      if (!reader.ok()) return std::nullopt;
      break;
  }

  while (reader.Remaining() > 0) {
    uint8 opt_type = reader.ReadU8();
    if (opt_type == kMobOptPad1) continue;
    uint8 opt_len = reader.ReadU8();
    std::string_view opt_data = reader.ReadBytes(opt_len);
    if (!reader.ok()) return std::nullopt;

    if (opt_type == kMobOptPadN) {
      continue;
    } else if (opt_type == kMobOptAlternateCoa) {
      if (opt_len != 16) return std::nullopt;
      WireReader opt_reader(opt_data);
      msg.alternate_care_of_address = opt_reader.ReadIpv6Address();
    } else if (opt_type == kMobOptNonceIndices) {
      if (opt_len != 4) return std::nullopt;
      WireReader opt_reader(opt_data);
      uint16 home_idx = opt_reader.ReadU16();
      uint16 coa_idx = opt_reader.ReadU16();
      msg.nonce_indices = std::make_pair(home_idx, coa_idx);
    } else if (opt_type == kMobOptAuthData) {
      if (opt_len != kAuthenticatorSize) return std::nullopt;
      std::array<uint8, kAuthenticatorSize> auth{};
      std::memcpy(auth.data(), opt_data.data(), kAuthenticatorSize);
      msg.authenticator = auth;
    } else if (opt_type == kMobOptRefreshAdvice) {
      if (opt_len != 2) return std::nullopt;
      WireReader opt_reader(opt_data);
      msg.refresh_advice_units = opt_reader.ReadU16();
    }
  }

  return msg;
}

bool VerifyMobilityHeaderAuthenticator(
    std::string_view raw_mobility_header, const std::array<uint8, 20>& k_bm,
    const IpAddress& care_of_address, const IpAddress& cn_address) {
  if (raw_mobility_header.size() < kMinMobilityHeaderSize) return false;
  size_t total_len =
      (static_cast<size_t>(static_cast<uint8>(raw_mobility_header[1])) + 1) *
      kHeaderAlignmentUnit;
  if (raw_mobility_header.size() < total_len) return false;

  uint8 mh_type = static_cast<uint8>(raw_mobility_header[2]);
  size_t options_offset = 0;
  if (mh_type == static_cast<uint8>(MobilityHeaderType::BindingUpdate) ||
      mh_type == static_cast<uint8>(MobilityHeaderType::BindingAcknowledgement))
    options_offset = 12;
  else
    return false;

  std::string zeroed(raw_mobility_header.substr(0, total_len));
  // Zero the Mobility Header checksum field (bytes 4..5).
  zeroed[4] = 0;
  zeroed[5] = 0;

  size_t offset = options_offset;
  std::array<uint8, kAuthenticatorSize> received_auth{};
  bool found_auth = false;
  while (offset < total_len) {
    uint8 opt_type = static_cast<uint8>(zeroed[offset]);
    if (opt_type == kMobOptPad1) {
      offset++;
      continue;
    }
    if (offset + 2 > total_len) return false;
    uint8 opt_len = static_cast<uint8>(zeroed[offset + 1]);
    if (offset + 2 + opt_len > total_len) return false;
    if (opt_type == kMobOptAuthData) {
      if (opt_len != kAuthenticatorSize) return false;
      std::memcpy(received_auth.data(), &zeroed[offset + 2],
                  kAuthenticatorSize);
      std::memset(&zeroed[offset + 2], 0, kAuthenticatorSize);
      found_auth = true;
      break;
    }
    offset += 2 + opt_len;
  }

  if (!found_auth) return false;
  auto expected =
      ComputeBindingAuthenticator(k_bm, care_of_address, cn_address, zeroed);
  uint8 diff = 0;
  for (size_t i = 0; i < kAuthenticatorSize; i++)
    diff |= (received_auth[i] ^ expected[i]);
  return diff == 0;
}

std::string BuildHomeAddressDestinationOptionsHeader(
    uint8 next_header, const IpAddress& home_address) {
  std::string out;
  out.reserve(24);
  WireWriter writer(out);
  writer.WriteU8(next_header);
  writer.WriteU8(kHdrExtLen24Bytes);
  // PadN of 4 bytes (0x01, 0x02, 0x00, 0x00) aligns option data to 8n + 6.
  writer.WriteU8(kMobOptPadN);
  writer.WriteU8(2);
  writer.WriteZeros(2);
  writer.WriteU8(kHomeAddressOptionType);
  writer.WriteU8(16);
  writer.WriteIpv6Address(home_address);
  return out;
}

std::optional<IpAddress> ParseHomeAddressDestinationOption(
    std::string_view options_body) {
  WireReader reader(options_body);
  while (reader.Remaining() > 0) {
    uint8 opt_type = reader.ReadU8();
    if (opt_type == kMobOptPad1) continue;
    uint8 opt_len = reader.ReadU8();
    std::string_view data = reader.ReadBytes(opt_len);
    if (!reader.ok()) return std::nullopt;
    if (opt_type == kHomeAddressOptionType) {
      if (opt_len != 16) return std::nullopt;
      WireReader addr_reader(data);
      IpAddress hoa = addr_reader.ReadIpv6Address();
      if (hoa.IsUnspecified() || hoa.IsMulticast() || hoa.IsLinkLocal())
        return std::nullopt;
      return hoa;
    }
  }
  return std::nullopt;
}

std::string BuildType2RoutingHeader(uint8 next_header,
                                    const IpAddress& home_address) {
  std::string out;
  out.reserve(kType2RoutingHeaderSize);
  WireWriter writer(out);
  writer.WriteU8(next_header);
  writer.WriteU8(kHdrExtLen24Bytes);
  writer.WriteU8(kType2RoutingHeaderType);
  writer.WriteU8(1);
  writer.WriteU32(0);
  writer.WriteIpv6Address(home_address);
  return out;
}

RoutingHeaderStatus ProcessMip6RoutingHeader(
    std::string& ipv6_packet, size_t routing_header_offset,
    const IpAddress& local_home_address, uint32& out_problem_pointer) {
  if (ipv6_packet.size() < kIpv6HeaderSize ||
      routing_header_offset + 4 > ipv6_packet.size())
    return RoutingHeaderStatus::Discard;

  uint8 hdr_ext_len =
      static_cast<uint8>(ipv6_packet[routing_header_offset + 1]);
  size_t rh_byte_len = (static_cast<size_t>(hdr_ext_len) + 1) * 8;
  if (routing_header_offset + rh_byte_len > ipv6_packet.size())
    return RoutingHeaderStatus::Discard;

  uint8 routing_type =
      static_cast<uint8>(ipv6_packet[routing_header_offset + 2]);
  uint8 segments_left =
      static_cast<uint8>(ipv6_packet[routing_header_offset + 3]);

  if (segments_left == 0) return RoutingHeaderStatus::NoSegmentsLeft;

  // Type 0 Routing Header (and any non-Type-2 Routing Header with segments
  // remaining) is rejected with ICMPv6 Parameter Problem pointing at the
  // Routing Type field (RFC 5095 / RFC 6275 §6.4).
  if (routing_type != kType2RoutingHeaderType) {
    out_problem_pointer = static_cast<uint32>(routing_header_offset + 2);
    return RoutingHeaderStatus::RejectedParameterProblem;
  }

  if (hdr_ext_len != kHdrExtLen24Bytes || segments_left != 1) {
    out_problem_pointer = static_cast<uint32>(routing_header_offset + 1);
    return RoutingHeaderStatus::RejectedParameterProblem;
  }

  WireReader reader(
      std::string_view(ipv6_packet).substr(routing_header_offset + 8, 16));
  IpAddress rh_home_address = reader.ReadIpv6Address();
  if (rh_home_address != local_home_address) return RoutingHeaderStatus::Discard;

  // Swap the IPv6 header's Destination Address (bytes 24..39) with the Home
  // Address in the Type 2 Routing Header (offset + 8..offset + 23), and
  // decrement Segments Left to 0 (RFC 6275 §6.4).
  std::array<uint8, 16> current_dest{};
  std::memcpy(current_dest.data(), &ipv6_packet[24], 16);
  std::memcpy(&ipv6_packet[24], local_home_address.bytes().data(), 16);
  std::memcpy(&ipv6_packet[routing_header_offset + 8], current_dest.data(), 16);
  ipv6_packet[routing_header_offset + 3] = 0;
  return RoutingHeaderStatus::ProcessedType2;
}

Mipv6CorrespondentNode::Mipv6CorrespondentNode(
    const IpAddress& cn_address, const std::array<uint8, 16>& secret_key)
    : cn_address_(cn_address), secret_key_(secret_key) {
  std::array<uint8, 8> initial_nonce = {0xA1, 0xB2, 0xC3, 0xD4,
                                        0xE5, 0xF6, 0x07, 0x18};
  nonces_[current_nonce_index_] = initial_nonce;
}

uint16 Mipv6CorrespondentNode::RotateNonce(
    const std::array<uint8, 8>& new_nonce) {
  current_nonce_index_++;
  if (current_nonce_index_ == 0) current_nonce_index_ = 1;
  nonces_[current_nonce_index_] = new_nonce;
  return current_nonce_index_;
}

std::optional<std::array<uint8, 8>> Mipv6CorrespondentNode::ComputeKeygenToken(
    const IpAddress& address, uint16 nonce_index, bool is_care_of) const {
  auto it = nonces_.find(nonce_index);
  if (it == nonces_.end()) return std::nullopt;

  std::string data;
  data.reserve(16 + 8 + 1);
  WireWriter writer(data);
  writer.WriteIpv6Address(address);
  writer.WriteBytes({reinterpret_cast<const char*>(it->second.data()), 8});
  writer.WriteU8(is_care_of ? 1 : 0);

  std::string_view key_view(reinterpret_cast<const char*>(secret_key_.data()),
                            secret_key_.size());
  auto hmac = ComputeHmacSha1(key_view, data);
  std::array<uint8, 8> token{};
  std::copy(hmac.begin(), hmac.begin() + 8, token.begin());
  return token;
}

std::optional<std::string> Mipv6CorrespondentNode::HandleMobilityPacket(
    const IpAddress& outer_source, const IpAddress& outer_destination,
    std::optional<IpAddress> home_address_option,
    std::string_view raw_mobility_header,
    std::chrono::steady_clock::time_point now) {
  IpAddress effective_source =
      home_address_option.has_value() ? *home_address_option : outer_source;
  std::optional<MobilityMessage> parsed = ParseMobilityHeader(
      effective_source, outer_destination, raw_mobility_header);
  if (!parsed.has_value()) return std::nullopt;

  switch (parsed->type) {
    case MobilityHeaderType::HomeTestInit: {
      auto token =
          ComputeKeygenToken(outer_source, current_nonce_index_, false);
      if (!token.has_value()) return std::nullopt;
      MobilityMessage hot;
      hot.type = MobilityHeaderType::HomeTest;
      hot.nonce_index = current_nonce_index_;
      hot.init_cookie = parsed->init_cookie;
      hot.keygen_token = *token;
      Ipv6Datagram dgram;
      dgram.source = cn_address_;
      dgram.destination = outer_source;
      dgram.next_header = kIpProtocolMobility;
      dgram.hop_limit = kDefaultMip6HopLimit;
      dgram.payload =
          SerializeMobilityHeader(cn_address_, outer_source, hot);
      return SerializeIpv6Datagram(dgram);
    }
    case MobilityHeaderType::CareOfTestInit: {
      auto token = ComputeKeygenToken(outer_source, current_nonce_index_, true);
      if (!token.has_value()) return std::nullopt;
      MobilityMessage cot;
      cot.type = MobilityHeaderType::CareOfTest;
      cot.nonce_index = current_nonce_index_;
      cot.init_cookie = parsed->init_cookie;
      cot.keygen_token = *token;
      Ipv6Datagram dgram;
      dgram.source = cn_address_;
      dgram.destination = outer_source;
      dgram.next_header = kIpProtocolMobility;
      dgram.hop_limit = kDefaultMip6HopLimit;
      dgram.payload =
          SerializeMobilityHeader(cn_address_, outer_source, cot);
      return SerializeIpv6Datagram(dgram);
    }
    case MobilityHeaderType::BindingUpdate: {
      if (parsed->home_registration) return std::nullopt;
      IpAddress hoa = home_address_option.value_or(outer_source);
      IpAddress coa = parsed->alternate_care_of_address.value_or(outer_source);
      if (!parsed->nonce_indices.has_value() ||
          !parsed->authenticator.has_value())
        return std::nullopt;

      uint16 home_idx = parsed->nonce_indices->first;
      uint16 coa_idx = parsed->nonce_indices->second;
      auto home_token = ComputeKeygenToken(hoa, home_idx, false);
      auto coa_token = ComputeKeygenToken(coa, coa_idx, true);

      BindingAckStatus status = BindingAckStatus::Accepted;
      std::optional<std::array<uint8, 20>> k_bm;
      if (!home_token.has_value() && !coa_token.has_value()) {
        status = BindingAckStatus::ExpiredNonces;
      } else if (!home_token.has_value()) {
        status = BindingAckStatus::ExpiredHomeNonceIndex;
      } else if (!coa_token.has_value()) {
        status = BindingAckStatus::ExpiredCareOfNonceIndex;
      } else {
        k_bm = DeriveBindingManagementKey(*home_token, *coa_token);
        if (!VerifyMobilityHeaderAuthenticator(raw_mobility_header, *k_bm, coa,
                                               cn_address_))
          return std::nullopt;
      }

      if (status == BindingAckStatus::Accepted) {
        auto existing = binding_cache_.find(hoa);
        if (existing != binding_cache_.end() &&
            !SequenceGreaterOrEqual(parsed->sequence_number,
                                    existing->second.sequence_number)) {
          status = BindingAckStatus::SequenceNumberOutOfWindow;
        } else if (parsed->lifetime_units == 0 || coa == hoa) {
          binding_cache_.erase(hoa);
        } else {
          BindingCacheEntry entry;
          entry.home_address = hoa;
          entry.care_of_address = coa;
          entry.sequence_number = parsed->sequence_number;
          entry.is_home_registration = false;
          entry.expires_at =
              now + std::chrono::seconds(
                        static_cast<uint64>(parsed->lifetime_units) *
                        kLifetimeUnitSeconds);
          binding_cache_[hoa] = entry;
        }
      }

      if (!parsed->acknowledge_requested &&
          status == BindingAckStatus::Accepted)
        return std::nullopt;

      MobilityMessage ba;
      ba.type = MobilityHeaderType::BindingAcknowledgement;
      ba.status = static_cast<uint8>(status);
      ba.sequence_number = parsed->sequence_number;
      ba.lifetime_units =
          (status == BindingAckStatus::Accepted) ? parsed->lifetime_units : 0;
      if (k_bm.has_value()) ba.authenticator = std::array<uint8, 12>{};

      std::string mh_bytes = SerializeMobilityHeader(
          cn_address_, hoa, ba, k_bm, coa, cn_address_);
      if (coa != hoa) {
        std::string rh = BuildType2RoutingHeader(kIpProtocolMobility, hoa);
        Ipv6Header ip6;
        ip6.source = cn_address_;
        ip6.destination = coa;
        ip6.next_header = static_cast<uint8>(Ipv6NextHeader::Routing);
        ip6.hop_limit = kDefaultMip6HopLimit;
        ip6.payload_length = static_cast<uint16>(rh.size() + mh_bytes.size());
        std::string pkt;
        AppendIpv6Header(ip6, pkt);
        pkt.append(rh);
        pkt.append(mh_bytes);
        return pkt;
      }
      Ipv6Datagram dgram;
      dgram.source = cn_address_;
      dgram.destination = hoa;
      dgram.next_header = kIpProtocolMobility;
      dgram.hop_limit = kDefaultMip6HopLimit;
      dgram.payload = std::move(mh_bytes);
      return SerializeIpv6Datagram(dgram);
    }
    default:
      return std::nullopt;
  }
}

std::optional<BindingCacheEntry> Mipv6CorrespondentNode::LookupBinding(
    const IpAddress& home_address,
    std::chrono::steady_clock::time_point now) const {
  auto it = binding_cache_.find(home_address);
  if (it == binding_cache_.end() || it->second.expires_at <= now)
    return std::nullopt;
  return it->second;
}

std::string Mipv6CorrespondentNode::TransformOutboundPacket(
    std::string_view ipv6_packet,
    std::chrono::steady_clock::time_point now) const {
  std::optional<Ipv6Header> header = ParseIpv6Header(ipv6_packet);
  if (!header.has_value()) return std::string(ipv6_packet);

  auto binding = LookupBinding(header->destination, now);
  if (!binding.has_value()) return std::string(ipv6_packet);

  std::string_view payload =
      ipv6_packet.substr(kIpv6HeaderSize, header->payload_length);
  std::string rh = BuildType2RoutingHeader(header->next_header,
                                           binding->home_address);

  Ipv6Header new_header = *header;
  new_header.destination = binding->care_of_address;
  new_header.next_header = static_cast<uint8>(Ipv6NextHeader::Routing);
  new_header.payload_length = static_cast<uint16>(rh.size() + payload.size());

  std::string out;
  out.reserve(kIpv6HeaderSize + new_header.payload_length);
  AppendIpv6Header(new_header, out);
  out.append(rh);
  out.append(payload);
  return out;
}

bool Mipv6CorrespondentNode::ProcessInboundPacket(
    std::string_view ipv6_packet, std::chrono::steady_clock::time_point now,
    std::string& out_normalized_packet,
    std::optional<std::string>& out_binding_error_packet) const {
  out_binding_error_packet = std::nullopt;
  std::optional<Ipv6Header> header = ParseIpv6Header(ipv6_packet);
  if (!header.has_value()) return false;

  if (header->next_header !=
      static_cast<uint8>(Ipv6NextHeader::DestinationOptions)) {
    out_normalized_packet.assign(ipv6_packet);
    return true;
  }

  size_t limit = kIpv6HeaderSize + header->payload_length;
  if (ipv6_packet.size() < limit || kIpv6HeaderSize + 8 > limit) return false;
  uint8 inner_next_header = static_cast<uint8>(ipv6_packet[kIpv6HeaderSize]);
  size_t dst_opt_len =
      (static_cast<size_t>(static_cast<uint8>(ipv6_packet[kIpv6HeaderSize + 1])) +
       1) *
      8;
  if (kIpv6HeaderSize + dst_opt_len > limit) return false;

  std::optional<IpAddress> hoa = ParseHomeAddressDestinationOption(
      ipv6_packet.substr(kIpv6HeaderSize + 2, dst_opt_len - 2));
  if (!hoa.has_value()) {
    out_normalized_packet.assign(ipv6_packet);
    return true;
  }

  if (inner_next_header != kIpProtocolMobility) {
    auto binding = LookupBinding(*hoa, now);
    if (!binding.has_value() || binding->care_of_address != header->source) {
      MobilityMessage be;
      be.type = MobilityHeaderType::BindingError;
      be.status = static_cast<uint8>(
          BindingErrorStatus::UnknownBindingForHomeAddressOption);
      be.error_home_address = *hoa;

      Ipv6Datagram dgram;
      dgram.source = cn_address_;
      dgram.destination = header->source;
      dgram.next_header = kIpProtocolMobility;
      dgram.hop_limit = kDefaultMip6HopLimit;
      dgram.payload =
          SerializeMobilityHeader(cn_address_, header->source, be);
      out_binding_error_packet = SerializeIpv6Datagram(dgram);
      return false;
    }
  }

  std::string_view remaining_payload = ipv6_packet.substr(
      kIpv6HeaderSize + dst_opt_len, header->payload_length - dst_opt_len);
  Ipv6Header normalized_hdr = *header;
  normalized_hdr.source = *hoa;
  normalized_hdr.next_header = inner_next_header;
  normalized_hdr.payload_length = static_cast<uint16>(remaining_payload.size());

  out_normalized_packet.clear();
  out_normalized_packet.reserve(kIpv6HeaderSize +
                                normalized_hdr.payload_length);
  AppendIpv6Header(normalized_hdr, out_normalized_packet);
  out_normalized_packet.append(remaining_payload);
  return true;
}

Mipv6MobileNode::Mipv6MobileNode(const IpAddress& home_address,
                                 const IpAddress& home_agent_address)
    : home_address_(home_address),
      home_agent_address_(home_agent_address),
      care_of_address_(home_address) {}

void Mipv6MobileNode::SetCareOfAddress(const IpAddress& care_of_address) {
  if (care_of_address_ != care_of_address) {
    care_of_address_ = care_of_address;
    home_registered_ = false;
    for (auto& [cn, state] : cn_states_) {
      state.care_of_nonce_index.reset();
      state.care_of_token.reset();
      state.binding_active = false;
    }
  }
}

std::string Mipv6MobileNode::BuildHomeAgentBindingUpdate(
    uint16 lifetime_units) {
  MobilityMessage bu;
  bu.type = MobilityHeaderType::BindingUpdate;
  bu.sequence_number = next_sequence_number_++;
  last_ha_sequence_number_ = bu.sequence_number;
  bu.acknowledge_requested = true;
  bu.home_registration = true;
  bu.link_local_compatibility = true;
  bu.key_management_mobility = true;
  bu.lifetime_units = IsAwayFromHome() ? lifetime_units : 0;
  bu.alternate_care_of_address = care_of_address_;

  std::string mh_bytes =
      SerializeMobilityHeader(home_address_, home_agent_address_, bu);
  std::string dst_opt = BuildHomeAddressDestinationOptionsHeader(
      kIpProtocolMobility, home_address_);

  Ipv6Header ip6;
  ip6.source = care_of_address_;
  ip6.destination = home_agent_address_;
  ip6.next_header = static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
  ip6.hop_limit = kDefaultMip6HopLimit;
  ip6.payload_length = static_cast<uint16>(dst_opt.size() + mh_bytes.size());

  std::string pkt;
  AppendIpv6Header(ip6, pkt);
  pkt.append(dst_opt);
  pkt.append(mh_bytes);
  return pkt;
}

bool Mipv6MobileNode::HandleHomeAgentBindingAck(std::string_view ipv6_packet) {
  std::optional<Ipv6Header> header = ParseIpv6Header(ipv6_packet);
  if (!header.has_value() || header->source != home_agent_address_)
    return false;

  std::string pkt(ipv6_packet.substr(0, kIpv6HeaderSize + header->payload_length));
  size_t offset = kIpv6HeaderSize;
  uint8 next_header = header->next_header;
  if (next_header == static_cast<uint8>(Ipv6NextHeader::Routing)) {
    uint32 problem_ptr = 0;
    if (ProcessMip6RoutingHeader(pkt, offset, home_address_, problem_ptr) !=
        RoutingHeaderStatus::ProcessedType2)
      return false;
    next_header = static_cast<uint8>(pkt[offset]);
    offset += kType2RoutingHeaderSize;
  }

  if (next_header != kIpProtocolMobility || offset >= pkt.size()) return false;
  auto msg = ParseMobilityHeader(home_agent_address_, home_address_,
                                 std::string_view(pkt).substr(offset));
  if (!msg.has_value() ||
      msg->type != MobilityHeaderType::BindingAcknowledgement)
    return false;
  if (msg->sequence_number != last_ha_sequence_number_ ||
      msg->status != static_cast<uint8>(BindingAckStatus::Accepted))
    return false;

  home_registered_ = true;
  return true;
}

std::pair<std::string, std::string> Mipv6MobileNode::InitiateReturnRoutability(
    const IpAddress& cn_address, const std::array<uint8, 8>& home_cookie,
    const std::array<uint8, 8>& care_of_cookie) {
  CorrespondentState& state = cn_states_[cn_address];
  state.home_cookie = home_cookie;
  state.care_of_cookie = care_of_cookie;
  state.home_nonce_index.reset();
  state.home_token.reset();
  state.care_of_nonce_index.reset();
  state.care_of_token.reset();
  state.binding_active = false;

  // HoTI is sent from home_address and reverse-tunneled through the Home Agent.
  MobilityMessage hoti;
  hoti.type = MobilityHeaderType::HomeTestInit;
  hoti.init_cookie = home_cookie;
  Ipv6Datagram hoti_inner;
  hoti_inner.source = home_address_;
  hoti_inner.destination = cn_address;
  hoti_inner.next_header = kIpProtocolMobility;
  hoti_inner.hop_limit = kDefaultMip6HopLimit;
  hoti_inner.payload =
      SerializeMobilityHeader(home_address_, cn_address, hoti);
  std::string inner_bytes = SerializeIpv6Datagram(hoti_inner);

  Ipv6Datagram hoti_outer;
  hoti_outer.source = care_of_address_;
  hoti_outer.destination = home_agent_address_;
  hoti_outer.next_header = kIpProtocolIpv6Encap;
  hoti_outer.hop_limit = kDefaultMip6HopLimit;
  hoti_outer.payload = std::move(inner_bytes);
  std::string hoti_tunneled = SerializeIpv6Datagram(hoti_outer);

  // CoTI is sent directly from care_of_address to cn_address.
  MobilityMessage coti;
  coti.type = MobilityHeaderType::CareOfTestInit;
  coti.init_cookie = care_of_cookie;
  Ipv6Datagram coti_dgram;
  coti_dgram.source = care_of_address_;
  coti_dgram.destination = cn_address;
  coti_dgram.next_header = kIpProtocolMobility;
  coti_dgram.hop_limit = kDefaultMip6HopLimit;
  coti_dgram.payload =
      SerializeMobilityHeader(care_of_address_, cn_address, coti);
  std::string coti_direct = SerializeIpv6Datagram(coti_dgram);

  return {hoti_tunneled, coti_direct};
}

std::optional<std::string> Mipv6MobileNode::HandleReturnRoutabilityResponse(
    const IpAddress& cn_address, const MobilityMessage& message,
    uint16 lifetime_units) {
  auto it = cn_states_.find(cn_address);
  if (it == cn_states_.end()) return std::nullopt;
  CorrespondentState& state = it->second;

  if (message.type == MobilityHeaderType::HomeTest) {
    if (message.init_cookie != state.home_cookie) return std::nullopt;
    state.home_nonce_index = message.nonce_index;
    state.home_token = message.keygen_token;
  } else if (message.type == MobilityHeaderType::CareOfTest) {
    if (message.init_cookie != state.care_of_cookie) return std::nullopt;
    state.care_of_nonce_index = message.nonce_index;
    state.care_of_token = message.keygen_token;
  } else {
    return std::nullopt;
  }

  if (!state.home_token.has_value() || !state.care_of_token.has_value())
    return std::nullopt;

  auto k_bm =
      DeriveBindingManagementKey(*state.home_token, *state.care_of_token);
  MobilityMessage bu;
  bu.type = MobilityHeaderType::BindingUpdate;
  bu.sequence_number = next_sequence_number_++;
  bu.acknowledge_requested = true;
  bu.home_registration = false;
  bu.lifetime_units = lifetime_units;
  bu.nonce_indices =
      std::make_pair(*state.home_nonce_index, *state.care_of_nonce_index);
  bu.authenticator = std::array<uint8, 12>{};

  std::string mh_bytes = SerializeMobilityHeader(
      home_address_, cn_address, bu, k_bm, care_of_address_, cn_address);
  std::string dst_opt = BuildHomeAddressDestinationOptionsHeader(
      kIpProtocolMobility, home_address_);

  Ipv6Header ip6;
  ip6.source = care_of_address_;
  ip6.destination = cn_address;
  ip6.next_header = static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
  ip6.hop_limit = kDefaultMip6HopLimit;
  ip6.payload_length = static_cast<uint16>(dst_opt.size() + mh_bytes.size());

  std::string pkt;
  AppendIpv6Header(ip6, pkt);
  pkt.append(dst_opt);
  pkt.append(mh_bytes);
  return pkt;
}

void Mipv6MobileNode::ConfirmCorrespondentBinding(const IpAddress& cn_address) {
  cn_states_[cn_address].binding_active = true;
}

std::string Mipv6MobileNode::PrepareOutboundPacket(
    std::string_view inner_ipv6_packet) const {
  if (!IsAwayFromHome()) return std::string(inner_ipv6_packet);

  std::optional<Ipv6Header> inner_hdr = ParseIpv6Header(inner_ipv6_packet);
  if (!inner_hdr.has_value()) return std::string(inner_ipv6_packet);

  auto it = cn_states_.find(inner_hdr->destination);
  if (it != cn_states_.end() && it->second.binding_active) {
    // Route-optimized: send from Care-of Address with Home Address Destination
    // Option (201) carrying the Mobile Node's Home Address.
    std::string_view inner_payload =
        inner_ipv6_packet.substr(kIpv6HeaderSize, inner_hdr->payload_length);
    std::string dst_opt = BuildHomeAddressDestinationOptionsHeader(
        inner_hdr->next_header, home_address_);
    Ipv6Header opt_hdr = *inner_hdr;
    opt_hdr.source = care_of_address_;
    opt_hdr.next_header =
        static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
    opt_hdr.payload_length =
        static_cast<uint16>(dst_opt.size() + inner_payload.size());
    std::string out;
    out.reserve(kIpv6HeaderSize + opt_hdr.payload_length);
    AppendIpv6Header(opt_hdr, out);
    out.append(dst_opt);
    out.append(inner_payload);
    return out;
  }

  // Bidirectional tunnel: encapsulate inner packet to the Home Agent.
  Ipv6Datagram outer;
  outer.source = care_of_address_;
  outer.destination = home_agent_address_;
  outer.next_header = kIpProtocolIpv6Encap;
  outer.hop_limit = kDefaultMip6HopLimit;
  outer.payload.assign(
      inner_ipv6_packet.substr(0, kIpv6HeaderSize + inner_hdr->payload_length));
  return SerializeIpv6Datagram(outer);
}

std::optional<std::string> Mipv6MobileNode::ProcessInboundPacket(
    std::string_view outer_ipv6_packet) const {
  std::optional<Ipv6Header> outer_hdr = ParseIpv6Header(outer_ipv6_packet);
  if (!outer_hdr.has_value()) return std::nullopt;

  // Case A: IPv6-in-IPv6 tunneled packet from the Home Agent.
  if (outer_hdr->next_header == kIpProtocolIpv6Encap &&
      outer_hdr->source == home_agent_address_ &&
      outer_hdr->destination == care_of_address_) {
    std::string_view inner =
        outer_ipv6_packet.substr(kIpv6HeaderSize, outer_hdr->payload_length);
    std::optional<Ipv6Header> inner_hdr = ParseIpv6Header(inner);
    if (!inner_hdr.has_value() || inner_hdr->destination != home_address_)
      return std::nullopt;
    return std::string(inner.substr(0, kIpv6HeaderSize + inner_hdr->payload_length));
  }

  // Case B: Route-optimized packet carrying a Type 2 Routing Header.
  if (outer_hdr->next_header == static_cast<uint8>(Ipv6NextHeader::Routing) &&
      outer_hdr->destination == care_of_address_) {
    std::string pkt(
        outer_ipv6_packet.substr(0, kIpv6HeaderSize + outer_hdr->payload_length));
    uint32 problem_ptr = 0;
    if (ProcessMip6RoutingHeader(pkt, kIpv6HeaderSize, home_address_,
                                 problem_ptr) !=
        RoutingHeaderStatus::ProcessedType2)
      return std::nullopt;
    return pkt;
  }

  return std::nullopt;
}

Mipv6HomeAgent::Mipv6HomeAgent(const IpAddress& ha_address,
                               const IpAddress& home_prefix,
                               uint8 home_prefix_length)
    : ha_address_(ha_address),
      home_prefix_(home_prefix),
      home_prefix_length_(home_prefix_length) {}

std::optional<std::string> Mipv6HomeAgent::HandleBindingUpdate(
    const IpAddress& outer_source,
    std::optional<IpAddress> home_address_option, const MobilityMessage& bu,
    std::chrono::steady_clock::time_point now) {
  if (bu.type != MobilityHeaderType::BindingUpdate || !bu.home_registration)
    return std::nullopt;

  IpAddress hoa = home_address_option.value_or(outer_source);
  IpAddress coa = bu.alternate_care_of_address.value_or(outer_source);

  BindingAckStatus status = BindingAckStatus::Accepted;
  if (!hoa.IsInPrefix(home_prefix_, home_prefix_length_)) {
    status = BindingAckStatus::NotHomeSubnet;
  } else {
    auto existing = bindings_.find(hoa);
    if (existing != bindings_.end() &&
        !SequenceGreaterOrEqual(bu.sequence_number,
                                existing->second.sequence_number)) {
      status = BindingAckStatus::SequenceNumberOutOfWindow;
    } else if (bu.lifetime_units == 0 || coa == hoa) {
      bindings_.erase(hoa);
      proxy_ndp_targets_.erase(hoa);
      proxy_ndp_targets_.erase(SolicitedNodeMulticastAddress(hoa));
    } else {
      BindingCacheEntry entry;
      entry.home_address = hoa;
      entry.care_of_address = coa;
      entry.sequence_number = bu.sequence_number;
      entry.is_home_registration = true;
      entry.expires_at =
          now + std::chrono::seconds(
                    static_cast<uint64>(bu.lifetime_units) *
                    kLifetimeUnitSeconds);
      bindings_[hoa] = entry;
      proxy_ndp_targets_.insert(hoa);
      proxy_ndp_targets_.insert(SolicitedNodeMulticastAddress(hoa));
    }
  }

  MobilityMessage ba;
  ba.type = MobilityHeaderType::BindingAcknowledgement;
  ba.status = static_cast<uint8>(status);
  ba.key_management_mobility = bu.key_management_mobility;
  ba.sequence_number = bu.sequence_number;
  ba.lifetime_units =
      (status == BindingAckStatus::Accepted) ? bu.lifetime_units : 0;

  std::string mh_bytes = SerializeMobilityHeader(ha_address_, hoa, ba);
  if (coa != hoa) {
    std::string rh = BuildType2RoutingHeader(kIpProtocolMobility, hoa);
    Ipv6Header ip6;
    ip6.source = ha_address_;
    ip6.destination = coa;
    ip6.next_header = static_cast<uint8>(Ipv6NextHeader::Routing);
    ip6.hop_limit = kDefaultMip6HopLimit;
    ip6.payload_length = static_cast<uint16>(rh.size() + mh_bytes.size());
    std::string pkt;
    AppendIpv6Header(ip6, pkt);
    pkt.append(rh);
    pkt.append(mh_bytes);
    return pkt;
  }

  Ipv6Datagram dgram;
  dgram.source = ha_address_;
  dgram.destination = hoa;
  dgram.next_header = kIpProtocolMobility;
  dgram.hop_limit = kDefaultMip6HopLimit;
  dgram.payload = std::move(mh_bytes);
  return SerializeIpv6Datagram(dgram);
}

bool Mipv6HomeAgent::ShouldProxyNdpFor(
    const IpAddress& target_address,
    std::chrono::steady_clock::time_point now) const {
  for (const auto& [hoa, entry] : bindings_) {
    if (entry.expires_at <= now) continue;
    if (target_address == hoa ||
        target_address == SolicitedNodeMulticastAddress(hoa))
      return true;
  }
  return false;
}

std::optional<std::string> Mipv6HomeAgent::InterceptAndTunnelPacket(
    std::string_view inner_ipv6_packet,
    std::chrono::steady_clock::time_point now) const {
  std::optional<Ipv6Header> inner_hdr = ParseIpv6Header(inner_ipv6_packet);
  if (!inner_hdr.has_value()) return std::nullopt;

  auto binding = LookupBinding(inner_hdr->destination, now);
  if (!binding.has_value()) return std::nullopt;

  Ipv6Datagram outer;
  outer.source = ha_address_;
  outer.destination = binding->care_of_address;
  outer.next_header = kIpProtocolIpv6Encap;
  outer.hop_limit = kDefaultMip6HopLimit;
  outer.payload.assign(
      inner_ipv6_packet.substr(0, kIpv6HeaderSize + inner_hdr->payload_length));
  return SerializeIpv6Datagram(outer);
}

std::optional<std::string> Mipv6HomeAgent::DecapsulateReverseTunnelPacket(
    std::string_view outer_ipv6_packet,
    std::chrono::steady_clock::time_point now) const {
  std::optional<Ipv6Header> outer_hdr = ParseIpv6Header(outer_ipv6_packet);
  if (!outer_hdr.has_value() ||
      outer_hdr->next_header != kIpProtocolIpv6Encap ||
      outer_hdr->destination != ha_address_)
    return std::nullopt;

  std::string_view inner =
      outer_ipv6_packet.substr(kIpv6HeaderSize, outer_hdr->payload_length);
  std::optional<Ipv6Header> inner_hdr = ParseIpv6Header(inner);
  if (!inner_hdr.has_value()) return std::nullopt;

  auto binding = LookupBinding(inner_hdr->source, now);
  if (!binding.has_value() || binding->care_of_address != outer_hdr->source)
    return std::nullopt;

  return std::string(inner.substr(0, kIpv6HeaderSize + inner_hdr->payload_length));
}

std::optional<BindingCacheEntry> Mipv6HomeAgent::LookupBinding(
    const IpAddress& home_address,
    std::chrono::steady_clock::time_point now) const {
  auto it = bindings_.find(home_address);
  if (it == bindings_.end() || it->second.expires_at <= now)
    return std::nullopt;
  return it->second;
}
