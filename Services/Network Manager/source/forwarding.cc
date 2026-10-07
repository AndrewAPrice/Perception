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

#include "forwarding.h"

#include <algorithm>

#include "ipv6_header.h"
#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// IPv4 version nibble.
constexpr uint8 kIpv4Version = 4;

// IPv6 version nibble.
constexpr uint8 kIpv6Version = 6;

// Minimum IPv4 header length in bytes.
constexpr size_t kMinIpv4HeaderSize = 20;

// Offset of the Don't Fragment flag bit in the 16-bit IPv4 flags_offset field.
constexpr uint16 kIpv4DontFragmentFlag = 0x4000;

// Offset of the More Fragments flag bit in the 16-bit IPv4 flags_offset field.
constexpr uint16 kIpv4MoreFragmentsFlag = 0x2000;

// Mask for the 13-bit fragment offset in the IPv4 flags_offset field.
constexpr uint16 kIpv4FragmentOffsetMask = 0x1FFF;

// Offset of the TTL field in an IPv4 header.
constexpr size_t kIpv4TtlOffset = 8;

// Offset of the Header Checksum field in an IPv4 header.
constexpr size_t kIpv4ChecksumOffset = 10;

// Offset of the Hop Limit field in an IPv6 header.
constexpr size_t kIpv6HopLimitOffset = 7;

// IPv4 protocol number for ICMPv4.
constexpr uint8 kIpProtocolIcmpv4 = 1;

// IPv6 Next Header value for ICMPv6.
constexpr uint8 kIpProtocolIcmpv6 = 58;

// Default TTL / Hop Limit for locally generated ICMP error packets.
constexpr uint8 kDefaultErrorTtl = 64;

// Maximum total IPv4 ICMP error packet size per RFC 1812.
constexpr size_t kMaxIpv4IcmpErrorPacketSize = 576;

// Size of an ICMPv4 or ICMPv6 error header (type, code, checksum, 4-byte word).
constexpr size_t kIcmpErrorHeaderSize = 8;

// ICMPv4 Destination Unreachable type.
constexpr uint8 kIcmpv4DestUnreachable = 3;

// ICMPv4 Code: Network Unreachable.
constexpr uint8 kIcmpv4CodeNetUnreachable = 0;

// ICMPv4 Code: Fragmentation Needed and DF Set.
constexpr uint8 kIcmpv4CodeFragNeeded = 4;

// ICMPv4 Time Exceeded type.
constexpr uint8 kIcmpv4TimeExceeded = 11;

// ICMPv4 Echo Reply type.
constexpr uint8 kIcmpv4EchoReply = 0;

// ICMPv4 Echo Request type.
constexpr uint8 kIcmpv4EchoRequest = 8;

// ICMPv6 Destination Unreachable type.
constexpr uint8 kIcmpv6DestUnreachable = 1;

// ICMPv6 Code: No route to destination.
constexpr uint8 kIcmpv6CodeNoRoute = 0;

// ICMPv6 Packet Too Big type.
constexpr uint8 kIcmpv6PacketTooBig = 2;

// ICMPv6 Time Exceeded type.
constexpr uint8 kIcmpv6TimeExceeded = 3;

// Returns the interface descriptor with `index`, or nullptr if not found.
const ForwardingInterface* FindInterface(
    std::span<const ForwardingInterface> interfaces, size_t index) {
  for (const ForwardingInterface& iface : interfaces) {
    if (iface.index == index) return &iface;
  }
  return nullptr;
}

// Returns true if `address` is assigned to any interface in `interfaces`.
bool IsLocalAddress(std::span<const ForwardingInterface> interfaces,
                    const IpAddress& address) {
  for (const ForwardingInterface& iface : interfaces) {
    for (const IpAddress& local : iface.addresses) {
      if (local == address) return true;
    }
  }
  return false;
}

// Returns true if `address` is non-forwardable (link-local, multicast,
// loopback, broadcast, or unspecified).
bool IsNonForwardableAddress(const IpAddress& address) {
  return address.IsUnspecified() || address.IsLinkLocal() ||
         address.IsMulticast() || address.IsLoopback() || address.IsBroadcast();
}

// Selects a local address of `family` on `iface` (preferring non-link-local).
IpAddress SelectRouterSourceAddress(const ForwardingInterface* iface,
                                    IpAddressFamily family) {
  if (iface == nullptr) return IpAddress();
  IpAddress fallback;
  for (const IpAddress& addr : iface->addresses) {
    if (addr.family() != family) continue;
    if (!addr.IsLinkLocal()) return addr;
    if (fallback.IsUnspecified()) fallback = addr;
  }
  return fallback;
}

// Computes the RFC 1071 ones'-complement checksum over `data` in big-endian.
uint16 ComputeOnesComplementChecksum(std::string_view data) {
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

// Computes the ICMPv6 checksum including the 40-byte IPv6 pseudo-header.
uint16 ComputeIcmpv6Checksum(const IpAddress& source,
                             const IpAddress& destination,
                             std::string_view icmp_payload) {
  std::string pseudo_and_payload;
  pseudo_and_payload.reserve(kIpv6HeaderSize + icmp_payload.size());
  WireWriter writer(pseudo_and_payload);
  writer.WriteIpv6Address(source);
  writer.WriteIpv6Address(destination);
  writer.WriteU32(static_cast<uint32>(icmp_payload.size()));
  writer.WriteZeros(3);
  writer.WriteU8(kIpProtocolIcmpv6);
  writer.WriteBytes(icmp_payload);
  return ComputeOnesComplementChecksum(pseudo_and_payload);
}

// Returns true if the IPv4 packet is an ICMPv4 error message (or non-first
// fragment), for which RFC 1812 forbids generating another ICMPv4 error.
bool SuppressIpv4IcmpError(std::string_view packet, size_t ihl, uint8 protocol,
                           uint16 flags_offset) {
  if ((flags_offset & kIpv4FragmentOffsetMask) != 0) return true;
  if (protocol != kIpProtocolIcmpv4) return false;
  if (packet.size() < ihl + 1) return true;
  uint8 icmp_type = static_cast<uint8>(packet[ihl]);
  return icmp_type != kIcmpv4EchoRequest && icmp_type != kIcmpv4EchoReply;
}

// Returns true if the IPv6 packet is an ICMPv6 error or redirect message, for
// which RFC 4443 §2.4(e) forbids generating another ICMPv6 error.
bool SuppressIpv6IcmpError(const Ipv6Header& header, std::string_view packet) {
  ExtensionWalkResult walk = WalkExtensionHeaders(header, packet);
  if (walk.status == ExtensionWalkStatus::UpperLayer &&
      walk.next_header == kIpProtocolIcmpv6 && !walk.payload.empty()) {
    uint8 icmp_type = static_cast<uint8>(walk.payload[0]);
    // ICMPv6 error messages have type 0..127; Redirect is 137.
    if (icmp_type < 128 || icmp_type == 137) return true;
  }
  return false;
}

// Builds a complete IPv4 + ICMPv4 error packet quoting `invoking_packet`.
std::string BuildIpv4IcmpErrorPacket(const IpAddress& router_source,
                                     const IpAddress& packet_source,
                                     uint8 icmp_type, uint8 icmp_code,
                                     uint32 header_word,
                                     std::string_view invoking_packet,
                                     size_t invoking_ihl) {
  size_t max_quote =
      std::min({invoking_packet.size(), invoking_ihl + 8,
                kMaxIpv4IcmpErrorPacketSize - kMinIpv4HeaderSize -
                    kIcmpErrorHeaderSize});
  std::string_view quoted = invoking_packet.substr(0, max_quote);

  std::string icmp;
  icmp.reserve(kIcmpErrorHeaderSize + quoted.size());
  WireWriter icmp_writer(icmp);
  icmp_writer.WriteU8(icmp_type);
  icmp_writer.WriteU8(icmp_code);
  icmp_writer.WriteU16(0);
  icmp_writer.WriteU32(header_word);
  icmp_writer.WriteBytes(quoted);
  uint16 icmp_checksum = ComputeOnesComplementChecksum(icmp);
  icmp_writer.PatchU16(2, icmp_checksum);

  uint16 total_length = static_cast<uint16>(kMinIpv4HeaderSize + icmp.size());
  std::string out;
  out.reserve(total_length);
  WireWriter ip_writer(out);
  ip_writer.WriteU8((kIpv4Version << 4) | 5);
  ip_writer.WriteU8(0);
  ip_writer.WriteU16(total_length);
  ip_writer.WriteU16(0);
  ip_writer.WriteU16(0);
  ip_writer.WriteU8(kDefaultErrorTtl);
  ip_writer.WriteU8(kIpProtocolIcmpv4);
  ip_writer.WriteU16(0);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    ip_writer.WriteU8(router_source.bytes()[i]);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    ip_writer.WriteU8(packet_source.bytes()[i]);
  uint16 ip_checksum = ComputeOnesComplementChecksum(
      std::string_view(out).substr(0, kMinIpv4HeaderSize));
  ip_writer.PatchU16(kIpv4ChecksumOffset, ip_checksum);
  ip_writer.WriteBytes(icmp);
  return out;
}

// Builds a complete IPv6 + ICMPv6 error packet quoting `invoking_packet` up to
// the 1280-byte minimum IPv6 MTU (RFC 4443 §2.4(c)).
std::string BuildIpv6IcmpErrorPacket(const IpAddress& router_source,
                                     const IpAddress& packet_source,
                                     uint8 icmp_type, uint8 icmp_code,
                                     uint32 header_word,
                                     std::string_view invoking_packet) {
  size_t max_quote =
      std::min(invoking_packet.size(),
               kIpv6MinimumMtu - kIpv6HeaderSize - kIcmpErrorHeaderSize);
  std::string_view quoted = invoking_packet.substr(0, max_quote);

  std::string icmp;
  icmp.reserve(kIcmpErrorHeaderSize + quoted.size());
  WireWriter icmp_writer(icmp);
  icmp_writer.WriteU8(icmp_type);
  icmp_writer.WriteU8(icmp_code);
  icmp_writer.WriteU16(0);
  icmp_writer.WriteU32(header_word);
  icmp_writer.WriteBytes(quoted);
  uint16 checksum = ComputeIcmpv6Checksum(router_source, packet_source, icmp);
  icmp_writer.PatchU16(2, checksum);

  Ipv6Datagram datagram;
  datagram.source = router_source;
  datagram.destination = packet_source;
  datagram.next_header = kIpProtocolIcmpv6;
  datagram.hop_limit = kDefaultErrorTtl;
  datagram.payload = std::move(icmp);
  return SerializeIpv6Datagram(datagram);
}

// Evaluates an IPv4 packet for forwarding.
ForwardingResult EvaluateIpv4Forwarding(
    std::string_view packet, size_t ingress_interface_index,
    const ForwardingConfig& config, const RoutingTable& routing_table,
    std::span<const ForwardingInterface> interfaces,
    const PmtuLookup& pmtu_lookup,
    std::optional<std::chrono::steady_clock::time_point> now) {
  ForwardingResult result;
  if (packet.size() < kMinIpv4HeaderSize) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }

  uint8 version_ihl = static_cast<uint8>(packet[0]);
  size_t ihl = static_cast<size_t>(version_ihl & 0x0F) * 4;
  if (ihl < kMinIpv4HeaderSize || packet.size() < ihl) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }

  WireReader reader(packet);
  reader.Skip(2);
  uint16 total_length = reader.ReadU16();
  if (total_length < ihl || packet.size() < total_length) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }
  packet = packet.substr(0, total_length);

  if (ComputeOnesComplementChecksum(packet.substr(0, ihl)) != 0) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }

  reader.Skip(2);
  uint16 flags_offset = reader.ReadU16();
  uint8 ttl = reader.ReadU8();
  uint8 protocol = reader.ReadU8();
  reader.Skip(2);

  std::string_view src_bytes = reader.ReadBytes(IpAddress::kV4Length);
  std::string_view dst_bytes = reader.ReadBytes(IpAddress::kV4Length);
  IpAddress source = IpAddress::FromBytes(
      IpAddressFamily::V4,
      {reinterpret_cast<const uint8*>(src_bytes.data()), src_bytes.size()});
  IpAddress destination = IpAddress::FromBytes(
      IpAddressFamily::V4,
      {reinterpret_cast<const uint8*>(dst_bytes.data()), dst_bytes.size()});

  if (IsLocalAddress(interfaces, destination)) {
    result.action = ForwardingAction::DeliverLocally;
    return result;
  }

  if (IsNonForwardableAddress(source) || IsNonForwardableAddress(destination)) {
    result.reason = ForwardingDropReason::LinkLocalOrMulticast;
    return result;
  }

  if (!config.ipv4_forwarding_enabled) {
    result.reason = ForwardingDropReason::ForwardingDisabled;
    return result;
  }

  const ForwardingInterface* ingress_iface =
      FindInterface(interfaces, ingress_interface_index);
  IpAddress router_src =
      SelectRouterSourceAddress(ingress_iface, IpAddressFamily::V4);

  auto emit_icmp_error = [&](ForwardingDropReason reason, uint8 type,
                             uint8 code, uint32 word) {
    result.reason = reason;
    if (SuppressIpv4IcmpError(packet, ihl, protocol, flags_offset) ||
        router_src.IsUnspecified()) {
      result.action = ForwardingAction::Drop;
      return result;
    }
    result.action = ForwardingAction::SendIcmpError;
    result.egress_interface_index = ingress_interface_index;
    result.next_hop = source;
    result.icmp_type = type;
    result.icmp_code = code;
    result.packet = BuildIpv4IcmpErrorPacket(router_src, source, type, code,
                                             word, packet, ihl);
    return result;
  };

  if (ttl <= 1) {
    return emit_icmp_error(ForwardingDropReason::TimeExceeded,
                           kIcmpv4TimeExceeded, 0, 0);
  }

  std::optional<ResolvedRoute> resolved = routing_table.Lookup(destination, now);
  if (!resolved.has_value()) {
    return emit_icmp_error(ForwardingDropReason::NoRoute,
                           kIcmpv4DestUnreachable, kIcmpv4CodeNetUnreachable, 0);
  }

  const ForwardingInterface* egress_iface =
      FindInterface(interfaces, resolved->interface_index);
  if (egress_iface == nullptr) {
    return emit_icmp_error(ForwardingDropReason::NoRoute,
                           kIcmpv4DestUnreachable, kIcmpv4CodeNetUnreachable, 0);
  }

  uint16 effective_mtu = egress_iface->mtu;
  if (pmtu_lookup) {
    if (std::optional<uint16> cached_pmtu = pmtu_lookup(destination))
      effective_mtu = std::min(effective_mtu, *cached_pmtu);
  }

  if (packet.size() > effective_mtu &&
      (flags_offset & kIpv4DontFragmentFlag) != 0) {
    result.reported_mtu = effective_mtu;
    return emit_icmp_error(ForwardingDropReason::PacketTooBig,
                           kIcmpv4DestUnreachable, kIcmpv4CodeFragNeeded,
                           static_cast<uint32>(effective_mtu));
  }

  std::string forwarded(packet);
  forwarded[kIpv4TtlOffset] = static_cast<char>(ttl - 1);
  forwarded[kIpv4ChecksumOffset] = 0;
  forwarded[kIpv4ChecksumOffset + 1] = 0;
  uint16 new_checksum = ComputeOnesComplementChecksum(
      std::string_view(forwarded).substr(0, ihl));
  forwarded[kIpv4ChecksumOffset] = static_cast<char>(new_checksum >> 8);
  forwarded[kIpv4ChecksumOffset + 1] = static_cast<char>(new_checksum & 0xFF);

  result.action = ForwardingAction::Forward;
  result.egress_interface_index = resolved->interface_index;
  result.next_hop = resolved->immediate_next_hop;
  result.packet = std::move(forwarded);
  return result;
}

// Evaluates an IPv6 packet for forwarding.
ForwardingResult EvaluateIpv6Forwarding(
    std::string_view packet, size_t ingress_interface_index,
    const ForwardingConfig& config, const RoutingTable& routing_table,
    std::span<const ForwardingInterface> interfaces,
    const PmtuLookup& pmtu_lookup,
    std::optional<std::chrono::steady_clock::time_point> now) {
  ForwardingResult result;
  std::optional<Ipv6Header> header = ParseIpv6Header(packet);
  if (!header.has_value()) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }
  packet = packet.substr(0, kIpv6HeaderSize + header->payload_length);

  if (IsLocalAddress(interfaces, header->destination)) {
    result.action = ForwardingAction::DeliverLocally;
    return result;
  }

  if (IsNonForwardableAddress(header->source) ||
      IsNonForwardableAddress(header->destination)) {
    result.reason = ForwardingDropReason::LinkLocalOrMulticast;
    return result;
  }

  if (!config.ipv6_forwarding_enabled) {
    result.reason = ForwardingDropReason::ForwardingDisabled;
    return result;
  }

  const ForwardingInterface* ingress_iface =
      FindInterface(interfaces, ingress_interface_index);
  IpAddress router_src =
      SelectRouterSourceAddress(ingress_iface, IpAddressFamily::V6);

  auto emit_icmpv6_error = [&](ForwardingDropReason reason, uint8 type,
                               uint8 code, uint32 word) {
    result.reason = reason;
    if (SuppressIpv6IcmpError(*header, packet) || router_src.IsUnspecified()) {
      result.action = ForwardingAction::Drop;
      return result;
    }
    result.action = ForwardingAction::SendIcmpError;
    result.egress_interface_index = ingress_interface_index;
    result.next_hop = header->source;
    result.icmp_type = type;
    result.icmp_code = code;
    result.packet = BuildIpv6IcmpErrorPacket(router_src, header->source, type,
                                             code, word, packet);
    return result;
  };

  if (header->hop_limit <= 1) {
    return emit_icmpv6_error(ForwardingDropReason::TimeExceeded,
                             kIcmpv6TimeExceeded, 0, 0);
  }

  std::optional<ResolvedRoute> resolved =
      routing_table.Lookup(header->destination, now);
  if (!resolved.has_value()) {
    return emit_icmpv6_error(ForwardingDropReason::NoRoute,
                             kIcmpv6DestUnreachable, kIcmpv6CodeNoRoute, 0);
  }

  const ForwardingInterface* egress_iface =
      FindInterface(interfaces, resolved->interface_index);
  if (egress_iface == nullptr) {
    return emit_icmpv6_error(ForwardingDropReason::NoRoute,
                             kIcmpv6DestUnreachable, kIcmpv6CodeNoRoute, 0);
  }

  uint16 effective_mtu = egress_iface->mtu;
  if (pmtu_lookup) {
    if (std::optional<uint16> cached_pmtu = pmtu_lookup(header->destination))
      effective_mtu = std::min(effective_mtu, *cached_pmtu);
  }

  if (packet.size() > effective_mtu) {
    result.reported_mtu = effective_mtu;
    return emit_icmpv6_error(ForwardingDropReason::PacketTooBig,
                             kIcmpv6PacketTooBig, 0,
                             static_cast<uint32>(effective_mtu));
  }

  std::string forwarded(packet);
  forwarded[kIpv6HopLimitOffset] = static_cast<char>(header->hop_limit - 1);

  result.action = ForwardingAction::Forward;
  result.egress_interface_index = resolved->interface_index;
  result.next_hop = resolved->immediate_next_hop;
  result.packet = std::move(forwarded);
  return result;
}

}  // namespace

ForwardingResult EvaluatePacketForwarding(
    std::string_view packet, size_t ingress_interface_index,
    const ForwardingConfig& config, const RoutingTable& routing_table,
    std::span<const ForwardingInterface> interfaces,
    const PmtuLookup& pmtu_lookup,
    std::optional<std::chrono::steady_clock::time_point> now) {
  ForwardingResult result;
  if (packet.empty()) {
    result.reason = ForwardingDropReason::MalformedPacket;
    return result;
  }

  uint8 version = static_cast<uint8>(packet[0]) >> 4;
  if (version == kIpv4Version) {
    return EvaluateIpv4Forwarding(packet, ingress_interface_index, config,
                                  routing_table, interfaces, pmtu_lookup, now);
  }
  if (version == kIpv6Version) {
    return EvaluateIpv6Forwarding(packet, ingress_interface_index, config,
                                  routing_table, interfaces, pmtu_lookup, now);
  }

  result.reason = ForwardingDropReason::MalformedPacket;
  return result;
}
