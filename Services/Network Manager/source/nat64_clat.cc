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

#include "nat64_clat.h"

#include <algorithm>
#include <array>

#include "checksum.h"
#include "ipv6_header.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Length of the RFC 8781 PREF64 RA option in 8-octet units (16 bytes total).
constexpr uint8 kPref64OptionUnits = 2;

// Total byte length of the RFC 8781 PREF64 RA option.
constexpr size_t kPref64OptionByteSize = 16;

// Number of prefix bytes carried inside the PREF64 RA option (96 bits).
constexpr size_t kPref64OptionPrefixBytes = 12;

// Maximum 13-bit Scaled Lifetime value in the PREF64 option (8191 * 8 = 65528).
constexpr uint32 kMaxPref64ScaledLifetime = 8191;

// Scale factor (seconds per unit) for PREF64 Scaled Lifetime.
constexpr uint32 kPref64LifetimeScale = 8;

// First RFC 7050 well-known IPv4 address for ipv4only.arpa (192.0.0.170).
constexpr std::array<uint8, 4> kRfc7050Ipv4A = {192, 0, 0, 170};

// Second RFC 7050 well-known IPv4 address for ipv4only.arpa (192.0.0.171).
constexpr std::array<uint8, 4> kRfc7050Ipv4B = {192, 0, 0, 171};

// Candidate RFC 6052 prefix lengths in descending order of specificity.
constexpr std::array<uint8, 6> kRfc6052PrefixLengths = {96, 64, 56, 48, 40, 32};

// Size of the fixed IPv4 header without options.
constexpr size_t kIpv4MinHeaderSize = 20;

// IPv4 version number.
constexpr uint8 kIpv4Version = 4;

// IPv4 Don't Fragment flag mask.
constexpr uint16 kIpv4DontFragmentMask = 0x4000;

// IPv4 More Fragments flag mask.
constexpr uint16 kIpv4MoreFragmentsMask = 0x2000;

// IPv4 fragment offset mask (in 8-byte units).
constexpr uint16 kIpv4FragmentOffsetMask = 0x1FFF;

// Protocol number for ICMPv4.
constexpr uint8 kProtocolIcmpv4 = 1;

// Protocol number for TCP.
constexpr uint8 kProtocolTcp = 6;

// Protocol number for UDP.
constexpr uint8 kProtocolUdp = 17;

// Protocol number for ICMPv6.
constexpr uint8 kProtocolIcmpv6 = 58;

// Minimum TCP header size in bytes.
constexpr size_t kMinTcpHeaderSize = 20;

// Offset of the checksum field inside a TCP header.
constexpr size_t kTcpChecksumOffset = 16;

// Minimum UDP header size in bytes.
constexpr size_t kMinUdpHeaderSize = 8;

// Offset of the checksum field inside a UDP header.
constexpr size_t kUdpChecksumOffset = 6;

// Minimum ICMP Echo header size in bytes.
constexpr size_t kMinIcmpEchoHeaderSize = 8;

// Offset of the checksum field inside an ICMP header.
constexpr size_t kIcmpChecksumOffset = 2;

// ICMPv4 Echo Reply type.
constexpr uint8 kIcmpv4EchoReply = 0;

// ICMPv4 Echo Request type.
constexpr uint8 kIcmpv4EchoRequest = 8;

// ICMPv6 Echo Request type.
constexpr uint8 kIcmpv6EchoRequest = 128;

// ICMPv6 Echo Reply type.
constexpr uint8 kIcmpv6EchoReply = 129;

// Returns the byte indices in a 16-byte IPv6 address where the 4 IPv4 octets
// are placed for a given RFC 6052 `prefix_length`.
std::optional<std::array<size_t, 4>> EmbeddedIpv4BytePositions(
    uint8 prefix_length) {
  switch (prefix_length) {
    case 96:
      return std::array<size_t, 4>{12, 13, 14, 15};
    case 64:
      return std::array<size_t, 4>{9, 10, 11, 12};
    case 56:
      return std::array<size_t, 4>{7, 9, 10, 11};
    case 48:
      return std::array<size_t, 4>{6, 7, 9, 10};
    case 40:
      return std::array<size_t, 4>{5, 6, 7, 9};
    case 32:
      return std::array<size_t, 4>{4, 5, 6, 7};
    default:
      return std::nullopt;
  }
}

// Converts an RFC 8781 3-bit Prefix Length Code (PLC) to a prefix length.
std::optional<uint8> PrefixLengthFromPlc(uint8 plc) {
  switch (plc) {
    case 0:
      return 96;
    case 1:
      return 64;
    case 2:
      return 56;
    case 3:
      return 48;
    case 4:
      return 40;
    case 5:
      return 32;
    default:
      return std::nullopt;
  }
}

// Converts a prefix length to an RFC 8781 3-bit Prefix Length Code (PLC).
std::optional<uint8> PlcFromPrefixLength(uint8 prefix_length) {
  switch (prefix_length) {
    case 96:
      return 0;
    case 64:
      return 1;
    case 56:
      return 2;
    case 48:
      return 3;
    case 40:
      return 4;
    case 32:
      return 5;
    default:
      return std::nullopt;
  }
}

// Clears all bits in `bytes` at and after `prefix_length`.
std::array<uint8, 16> MaskIpv6PrefixBytes(std::array<uint8, 16> bytes,
                                          uint8 prefix_length) {
  size_t keep_bytes = prefix_length / 8;
  for (size_t i = keep_bytes; i < bytes.size(); i++) bytes[i] = 0;
  return bytes;
}

}  // namespace

bool Nat64Prefix::IsValid() const {
  return prefix.IsV6() && EmbeddedIpv4BytePositions(prefix_length).has_value();
}

Nat64Prefix WellKnownNat64Prefix() {
  return {*IpAddress::Parse("64:ff9b::"), 96, 0};
}

IpAddress ClatLocalIpv4Address() { return IpAddress::V4(192, 0, 0, 4); }

std::optional<Nat64Prefix> ParsePref64RaOption(std::string_view option_bytes) {
  if (option_bytes.size() != kPref64OptionByteSize) return std::nullopt;
  WireReader reader(option_bytes);
  uint8 type = reader.ReadU8();
  uint8 length = reader.ReadU8();
  if (type != kNdOptionTypePref64 || length != kPref64OptionUnits)
    return std::nullopt;

  uint16 scaled_and_plc = reader.ReadU16();
  uint32 lifetime_seconds =
      static_cast<uint32>(scaled_and_plc >> 3) * kPref64LifetimeScale;
  auto prefix_len = PrefixLengthFromPlc(scaled_and_plc & 0x07);
  if (!prefix_len) return std::nullopt;

  std::array<uint8, 16> raw_bytes{};
  std::string_view prefix_96 = reader.ReadBytes(kPref64OptionPrefixBytes);
  for (size_t i = 0; i < kPref64OptionPrefixBytes; i++)
    raw_bytes[i] = static_cast<uint8>(prefix_96[i]);

  raw_bytes = MaskIpv6PrefixBytes(raw_bytes, *prefix_len);
  return Nat64Prefix{IpAddress::V6(raw_bytes), *prefix_len, lifetime_seconds};
}

std::optional<std::string> EncodePref64RaOption(
    const Nat64Prefix& nat64_prefix) {
  if (!nat64_prefix.IsValid()) return std::nullopt;
  auto plc = PlcFromPrefixLength(nat64_prefix.prefix_length);
  if (!plc) return std::nullopt;

  uint32 scaled_lifetime =
      std::min(nat64_prefix.lifetime_seconds / kPref64LifetimeScale,
               kMaxPref64ScaledLifetime);
  uint16 scaled_and_plc =
      static_cast<uint16>((scaled_lifetime << 3) | (*plc & 0x07));

  std::array<uint8, 16> masked =
      MaskIpv6PrefixBytes(nat64_prefix.prefix.bytes(), nat64_prefix.prefix_length);

  std::string out;
  out.reserve(kPref64OptionByteSize);
  WireWriter writer(out);
  writer.WriteU8(kNdOptionTypePref64);
  writer.WriteU8(kPref64OptionUnits);
  writer.WriteU16(scaled_and_plc);
  for (size_t i = 0; i < kPref64OptionPrefixBytes; i++)
    writer.WriteU8(masked[i]);
  return out;
}

std::optional<Nat64Prefix> DiscoverNat64PrefixFromDns64Answers(
    std::span<const IpAddress> aaaa_addresses) {
  IpAddress target_a = IpAddress::V4(kRfc7050Ipv4A);
  IpAddress target_b = IpAddress::V4(kRfc7050Ipv4B);

  for (const IpAddress& addr : aaaa_addresses) {
    if (!addr.IsV6()) continue;
    for (uint8 prefix_len : kRfc6052PrefixLengths) {
      Nat64Prefix candidate{
          IpAddress::V6(MaskIpv6PrefixBytes(addr.bytes(), prefix_len)),
          prefix_len, 0};
      auto synth_a = SynthesizeIpv6FromIpv4(candidate, target_a);
      auto synth_b = SynthesizeIpv6FromIpv4(candidate, target_b);
      if ((synth_a && *synth_a == addr) || (synth_b && *synth_b == addr))
        return candidate;
    }
  }
  return std::nullopt;
}

std::optional<IpAddress> SynthesizeIpv6FromIpv4(
    const Nat64Prefix& nat64_prefix, const IpAddress& ipv4_address) {
  if (!nat64_prefix.IsValid() || !ipv4_address.IsV4()) return std::nullopt;
  auto positions = EmbeddedIpv4BytePositions(nat64_prefix.prefix_length);
  if (!positions) return std::nullopt;

  std::array<uint8, 16> v6_bytes =
      MaskIpv6PrefixBytes(nat64_prefix.prefix.bytes(), nat64_prefix.prefix_length);
  const auto& v4_bytes = ipv4_address.bytes();
  for (size_t i = 0; i < 4; i++) v6_bytes[(*positions)[i]] = v4_bytes[i];
  // Byte 8 (bits 64..71) is the RFC 6052 `u` octet and must be zero for all
  // prefix lengths shorter than /96.
  if (nat64_prefix.prefix_length < 96) v6_bytes[8] = 0;
  return IpAddress::V6(v6_bytes);
}

std::optional<IpAddress> ExtractIpv4FromSynthesizedIpv6(
    const Nat64Prefix& nat64_prefix, const IpAddress& ipv6_address) {
  if (!nat64_prefix.IsValid() || !ipv6_address.IsV6()) return std::nullopt;
  if (!ipv6_address.IsInPrefix(nat64_prefix.prefix, nat64_prefix.prefix_length))
    return std::nullopt;
  auto positions = EmbeddedIpv4BytePositions(nat64_prefix.prefix_length);
  if (!positions) return std::nullopt;

  const auto& v6_bytes = ipv6_address.bytes();
  return IpAddress::V4(v6_bytes[(*positions)[0]], v6_bytes[(*positions)[1]],
                       v6_bytes[(*positions)[2]], v6_bytes[(*positions)[3]]);
}

std::vector<DnsAddressRecord> SynthesizeDns64Records(
    const Nat64Prefix& nat64_prefix,
    std::span<const DnsAddressRecord> aaaa_records,
    std::span<const DnsAddressRecord> a_records) {
  if (!aaaa_records.empty())
    return std::vector<DnsAddressRecord>(aaaa_records.begin(),
                                         aaaa_records.end());
  if (!nat64_prefix.IsValid()) return {};

  std::vector<DnsAddressRecord> synthesized;
  synthesized.reserve(a_records.size());
  for (const DnsAddressRecord& record : a_records) {
    if (!record.address.IsV4()) continue;
    if (auto v6 = SynthesizeIpv6FromIpv4(nat64_prefix, record.address))
      synthesized.push_back({*v6, record.ttl});
  }
  return synthesized;
}

std::optional<std::string> TranslateIpv4ToIpv6(
    std::string_view ipv4_packet, const IpAddress& clat_ipv6_address,
    const Nat64Prefix& nat64_prefix) {
  if (!clat_ipv6_address.IsV6() || !nat64_prefix.IsValid())
    return std::nullopt;
  if (ipv4_packet.size() < kIpv4MinHeaderSize) return std::nullopt;

  uint8 version_ihl = static_cast<uint8>(ipv4_packet[0]);
  if ((version_ihl >> 4) != kIpv4Version) return std::nullopt;
  size_t ihl = static_cast<size_t>(version_ihl & 0x0F) * 4;
  if (ihl < kIpv4MinHeaderSize || ipv4_packet.size() < ihl)
    return std::nullopt;
  if (InternetChecksum(ipv4_packet.substr(0, ihl)) != 0) return std::nullopt;

  WireReader reader(ipv4_packet);
  reader.Skip(1);
  uint8 tos = reader.ReadU8();
  uint16 total_length = reader.ReadU16();
  if (total_length < ihl || total_length > ipv4_packet.size())
    return std::nullopt;
  uint16 identification = reader.ReadU16();
  uint16 flags_offset = reader.ReadU16();
  uint8 ttl = reader.ReadU8();
  uint8 protocol = reader.ReadU8();
  reader.Skip(6);  // Header checksum (2) + IPv4 source (4).
  uint8 d0 = reader.ReadU8();
  uint8 d1 = reader.ReadU8();
  uint8 d2 = reader.ReadU8();
  uint8 d3 = reader.ReadU8();
  IpAddress ipv4_dst = IpAddress::V4(d0, d1, d2, d3);

  auto ipv6_dst = SynthesizeIpv6FromIpv4(nat64_prefix, ipv4_dst);
  if (!ipv6_dst) return std::nullopt;

  bool more_fragments = (flags_offset & kIpv4MoreFragmentsMask) != 0;
  uint16 fragment_offset =
      static_cast<uint16>((flags_offset & kIpv4FragmentOffsetMask) * 8);
  bool is_fragmented = more_fragments || fragment_offset > 0;

  std::string payload(ipv4_packet.substr(ihl, total_length - ihl));
  uint8 next_header = protocol;

  if (fragment_offset == 0) {
    if (protocol == kProtocolTcp) {
      if (payload.size() < kMinTcpHeaderSize) return std::nullopt;
      WireWriter patcher(payload);
      patcher.PatchU16(kTcpChecksumOffset, 0);
      uint16 csum = TransportChecksum(clat_ipv6_address, *ipv6_dst,
                                      kProtocolTcp, payload);
      patcher.PatchU16(kTcpChecksumOffset, csum);
    } else if (protocol == kProtocolUdp) {
      if (payload.size() < kMinUdpHeaderSize) return std::nullopt;
      WireWriter patcher(payload);
      patcher.PatchU16(kUdpChecksumOffset, 0);
      uint16 csum = TransportChecksum(clat_ipv6_address, *ipv6_dst,
                                      kProtocolUdp, payload);
      if (csum == 0) csum = 0xFFFF;
      patcher.PatchU16(kUdpChecksumOffset, csum);
    } else if (protocol == kProtocolIcmpv4) {
      if (payload.size() < kMinIcmpEchoHeaderSize) return std::nullopt;
      uint8 icmp_type = static_cast<uint8>(payload[0]);
      uint8 icmp_code = static_cast<uint8>(payload[1]);
      if (icmp_code != 0) return std::nullopt;
      if (icmp_type == kIcmpv4EchoRequest)
        payload[0] = static_cast<char>(kIcmpv6EchoRequest);
      else if (icmp_type == kIcmpv4EchoReply)
        payload[0] = static_cast<char>(kIcmpv6EchoReply);
      else
        return std::nullopt;
      next_header = kProtocolIcmpv6;
      WireWriter patcher(payload);
      patcher.PatchU16(kIcmpChecksumOffset, 0);
      uint16 csum = TransportChecksum(clat_ipv6_address, *ipv6_dst,
                                      kProtocolIcmpv6, payload);
      patcher.PatchU16(kIcmpChecksumOffset, csum);
    } else {
      return std::nullopt;
    }
  } else if (protocol == kProtocolIcmpv4) {
    next_header = kProtocolIcmpv6;
  } else if (protocol != kProtocolTcp && protocol != kProtocolUdp) {
    return std::nullopt;
  }

  Ipv6Header v6_hdr;
  v6_hdr.traffic_class = tos;
  v6_hdr.hop_limit = ttl;
  v6_hdr.source = clat_ipv6_address;
  v6_hdr.destination = *ipv6_dst;
  v6_hdr.next_header =
      is_fragmented ? static_cast<uint8>(Ipv6NextHeader::Fragment) : next_header;
  v6_hdr.payload_length =
      static_cast<uint16>(payload.size() + (is_fragmented ? 8 : 0));

  std::string out;
  out.reserve(kIpv6HeaderSize + v6_hdr.payload_length);
  AppendIpv6Header(v6_hdr, out);
  if (is_fragmented) {
    WireWriter writer(out);
    writer.WriteU8(next_header);
    writer.WriteU8(0);
    writer.WriteU16(
        static_cast<uint16>((fragment_offset & 0xFFF8) | (more_fragments ? 1 : 0)));
    writer.WriteU32(identification);
  }
  out.append(payload);
  return out;
}

std::optional<std::string> TranslateIpv6ToIpv4(
    std::string_view ipv6_packet, const IpAddress& clat_ipv6_address,
    const Nat64Prefix& nat64_prefix, const IpAddress& clat_ipv4_address) {
  if (!clat_ipv6_address.IsV6() || !clat_ipv4_address.IsV4() ||
      !nat64_prefix.IsValid())
    return std::nullopt;

  auto v6_hdr = ParseIpv6Header(ipv6_packet);
  if (!v6_hdr) return std::nullopt;
  if (v6_hdr->destination != clat_ipv6_address) return std::nullopt;
  auto ipv4_src = ExtractIpv4FromSynthesizedIpv6(nat64_prefix, v6_hdr->source);
  if (!ipv4_src) return std::nullopt;

  auto walk = WalkExtensionHeaders(*v6_hdr, ipv6_packet);
  if (walk.status != ExtensionWalkStatus::UpperLayer &&
      walk.status != ExtensionWalkStatus::Fragment)
    return std::nullopt;

  std::string payload(walk.payload);
  uint8 v4_protocol = walk.next_header;
  uint16 fragment_offset = walk.fragment ? walk.fragment->offset : 0;
  bool more_fragments = walk.fragment ? walk.fragment->more_fragments : false;
  uint16 identification =
      walk.fragment ? static_cast<uint16>(walk.fragment->identification & 0xFFFF)
                    : 0;

  if (fragment_offset == 0) {
    if (walk.next_header == kProtocolTcp) {
      if (payload.size() < kMinTcpHeaderSize) return std::nullopt;
      WireWriter patcher(payload);
      patcher.PatchU16(kTcpChecksumOffset, 0);
      uint16 csum = TransportChecksum(*ipv4_src, clat_ipv4_address,
                                      kProtocolTcp, payload);
      patcher.PatchU16(kTcpChecksumOffset, csum);
    } else if (walk.next_header == kProtocolUdp) {
      if (payload.size() < kMinUdpHeaderSize) return std::nullopt;
      WireWriter patcher(payload);
      patcher.PatchU16(kUdpChecksumOffset, 0);
      uint16 csum = TransportChecksum(*ipv4_src, clat_ipv4_address,
                                      kProtocolUdp, payload);
      if (csum == 0) csum = 0xFFFF;
      patcher.PatchU16(kUdpChecksumOffset, csum);
    } else if (walk.next_header == kProtocolIcmpv6) {
      if (payload.size() < kMinIcmpEchoHeaderSize) return std::nullopt;
      uint8 icmp6_type = static_cast<uint8>(payload[0]);
      uint8 icmp6_code = static_cast<uint8>(payload[1]);
      if (icmp6_code != 0) return std::nullopt;
      if (icmp6_type == kIcmpv6EchoRequest)
        payload[0] = static_cast<char>(kIcmpv4EchoRequest);
      else if (icmp6_type == kIcmpv6EchoReply)
        payload[0] = static_cast<char>(kIcmpv4EchoReply);
      else
        return std::nullopt;
      v4_protocol = kProtocolIcmpv4;
      WireWriter patcher(payload);
      patcher.PatchU16(kIcmpChecksumOffset, 0);
      uint16 csum = InternetChecksum(payload);
      patcher.PatchU16(kIcmpChecksumOffset, csum);
    } else {
      return std::nullopt;
    }
  } else if (walk.next_header == kProtocolIcmpv6) {
    v4_protocol = kProtocolIcmpv4;
  } else if (walk.next_header != kProtocolTcp &&
             walk.next_header != kProtocolUdp) {
    return std::nullopt;
  }

  uint16 flags_and_offset = 0;
  if (walk.fragment.has_value()) {
    flags_and_offset = static_cast<uint16>(
        (more_fragments ? kIpv4MoreFragmentsMask : 0) | (fragment_offset / 8));
  } else if (kIpv4MinHeaderSize + payload.size() > kIpv6MinimumMtu) {
    flags_and_offset = kIpv4DontFragmentMask;
  }

  std::string out;
  uint16 total_length =
      static_cast<uint16>(kIpv4MinHeaderSize + payload.size());
  out.reserve(total_length);
  WireWriter writer(out);
  writer.WriteU8(0x45);
  writer.WriteU8(v6_hdr->traffic_class);
  writer.WriteU16(total_length);
  writer.WriteU16(identification);
  writer.WriteU16(flags_and_offset);
  writer.WriteU8(v6_hdr->hop_limit);
  writer.WriteU8(v4_protocol);
  writer.WriteU16(0);  // Checksum placeholder.
  const auto& src = ipv4_src->bytes();
  const auto& dst = clat_ipv4_address.bytes();
  for (size_t i = 0; i < 4; i++) writer.WriteU8(src[i]);
  for (size_t i = 0; i < 4; i++) writer.WriteU8(dst[i]);
  uint16 hdr_csum =
      InternetChecksum(std::string_view(out.data(), kIpv4MinHeaderSize));
  writer.PatchU16(10, hdr_csum);
  writer.WriteBytes(payload);
  return out;
}
