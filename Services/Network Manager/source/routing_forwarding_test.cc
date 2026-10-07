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

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "forwarding.h"
#include "ipv6_header.h"
#include "routing_table.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// IPv4 version nibble.
constexpr uint8 kIpv4Version = 4;

// Minimum IPv4 header size in bytes.
constexpr size_t kMinIpv4HeaderSize = 20;

// Offset of the checksum field in the IPv4 header.
constexpr size_t kIpv4ChecksumOffset = 10;

// Offset of the TTL field in the IPv4 header.
constexpr size_t kIpv4TtlOffset = 8;

// Offset of the Hop Limit field in the IPv6 header.
constexpr size_t kIpv6HopLimitOffset = 7;

// IPv4 Don't Fragment flag.
constexpr uint16 kIpv4DfFlag = 0x4000;

// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;

// ICMPv4 protocol number.
constexpr uint8 kProtocolIcmpv4 = 1;

// ICMPv6 protocol number.
constexpr uint8 kProtocolIcmpv6 = 58;

// Computes RFC 1071 ones'-complement checksum over `data`.
uint16 Checksum(std::string_view data) {
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

// Builds a test IPv4 packet with a valid header checksum.
std::string MakeIpv4Packet(const IpAddress& src, const IpAddress& dst,
                           uint8 ttl, bool dont_fragment,
                           size_t payload_length,
                           uint8 protocol = kProtocolUdp,
                           uint8 first_payload_byte = 0) {
  uint16 total_length =
      static_cast<uint16>(kMinIpv4HeaderSize + payload_length);
  std::string out;
  out.reserve(total_length);
  WireWriter writer(out);
  writer.WriteU8((kIpv4Version << 4) | 5);
  writer.WriteU8(0);
  writer.WriteU16(total_length);
  writer.WriteU16(0x1234);
  writer.WriteU16(dont_fragment ? kIpv4DfFlag : 0);
  writer.WriteU8(ttl);
  writer.WriteU8(protocol);
  writer.WriteU16(0);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    writer.WriteU8(src.bytes()[i]);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    writer.WriteU8(dst.bytes()[i]);
  uint16 csum = Checksum(std::string_view(out).substr(0, kMinIpv4HeaderSize));
  writer.PatchU16(kIpv4ChecksumOffset, csum);
  if (payload_length > 0) {
    writer.WriteU8(first_payload_byte);
    writer.WriteZeros(payload_length - 1);
  }
  return out;
}

// Builds a test IPv6 packet.
std::string MakeIpv6Packet(const IpAddress& src, const IpAddress& dst,
                           uint8 hop_limit, size_t payload_length,
                           uint8 next_header = kProtocolUdp,
                           uint8 first_payload_byte = 0) {
  Ipv6Datagram datagram;
  datagram.source = src;
  datagram.destination = dst;
  datagram.hop_limit = hop_limit;
  datagram.next_header = next_header;
  datagram.payload.assign(payload_length, '\0');
  if (payload_length > 0)
    datagram.payload[0] = static_cast<char>(first_payload_byte);
  return SerializeIpv6Datagram(datagram);
}

}  // namespace

TEST(RoutingTableLongestPrefixMatchAndTieBreaking) {
  RoutingTable table;
  auto now = std::chrono::steady_clock::now();

  // Default route via RA on interface 0.
  table.AddRaRoute(*IpAddress::Parse("::"), 0, *IpAddress::Parse("fe80::1"), 0,
                   now + std::chrono::seconds(600), /*metric=*/10);
  // /48 static route via interface 1.
  table.AddStaticRoute(*IpAddress::Parse("2001:db8:1::"), 48,
                       *IpAddress::Parse("fe80::2"), 1, /*metric=*/20);
  // /64 connected route on interface 2.
  table.AddConnectedRoute(*IpAddress::Parse("2001:db8:1:2::"), 64, 2);

  // Matches /64 connected route (on-link: immediate_next_hop == destination).
  IpAddress dst_connected = *IpAddress::Parse("2001:db8:1:2::99");
  auto r1 = table.Lookup(dst_connected, now);
  EXPECT(true, r1.has_value());
  EXPECT((size_t)2, r1->interface_index);
  EXPECT(true, r1->immediate_next_hop == dst_connected);
  EXPECT(true, r1->route.IsOnLink());

  // Matches /48 static route.
  IpAddress dst_static = *IpAddress::Parse("2001:db8:1:3::99");
  auto r2 = table.Lookup(dst_static, now);
  EXPECT(true, r2.has_value());
  EXPECT((size_t)1, r2->interface_index);
  EXPECT(true, r2->immediate_next_hop == *IpAddress::Parse("fe80::2"));

  // Matches ::/0 default RA route.
  IpAddress dst_default = *IpAddress::Parse("2607:f8b0:4004:800::200e");
  auto r3 = table.Lookup(dst_default, now);
  EXPECT(true, r3.has_value());
  EXPECT((size_t)0, r3->interface_index);
  EXPECT(true, r3->immediate_next_hop == *IpAddress::Parse("fe80::1"));

  // Administrative distance tie-breaking: a static ::/0 outranks RA ::/0.
  table.AddStaticRoute(*IpAddress::Parse("::"), 0,
                       *IpAddress::Parse("fe80::99"), 1, /*metric=*/500);
  auto r4 = table.Lookup(dst_default, now);
  EXPECT(true, r4.has_value());
  EXPECT((size_t)1, r4->interface_index);
  EXPECT(true, r4->immediate_next_hop == *IpAddress::Parse("fe80::99"));

  // Metric tie-breaking: lower metric wins when prefix length and distance tie.
  table.AddStaticRoute(*IpAddress::Parse("10.10.0.0"), 16,
                       IpAddress::V4(10, 0, 2, 2), 0, /*metric=*/100);
  table.AddStaticRoute(*IpAddress::Parse("10.10.0.0"), 16,
                       IpAddress::V4(10, 0, 2, 3), 1, /*metric=*/10);
  auto r5 = table.Lookup(IpAddress::V4(10, 10, 5, 6), now);
  EXPECT(true, r5.has_value());
  EXPECT((size_t)1, r5->interface_index);
  EXPECT(true, r5->immediate_next_hop == IpAddress::V4(10, 0, 2, 3));
}

TEST(RoutingTableExpiryAndRemoval) {
  RoutingTable table;
  auto now = std::chrono::steady_clock::now();

  table.AddRaRoute(*IpAddress::Parse("fec0::"), 64,
                   *IpAddress::Parse("fe80::2"), 0,
                   now + std::chrono::seconds(30));
  EXPECT(true, table.Lookup(*IpAddress::Parse("fec0::1234"), now).has_value());
  EXPECT(false,
         table
             .Lookup(*IpAddress::Parse("fec0::1234"),
                     now + std::chrono::seconds(31))
             .has_value());

  table.PurgeExpired(now + std::chrono::seconds(31));
  EXPECT((size_t)0, table.Routes().size());
}

TEST(ForwardingDisabledByDefaultAndDropsNonForwardable) {
  ForwardingConfig default_config;
  EXPECT(false, default_config.ipv4_forwarding_enabled);
  EXPECT(false, default_config.ipv6_forwarding_enabled);

  RoutingTable routes;
  routes.AddConnectedRoute(IpAddress::V4(192, 168, 2, 0), 24, 1);
  routes.AddConnectedRoute(*IpAddress::Parse("2001:db8:2::"), 64, 1);

  std::vector<ForwardingInterface> interfaces = {
      {0, 1500, {IpAddress::V4(192, 168, 1, 1), *IpAddress::Parse("2001:db8:1::1")}},
      {1, 1500, {IpAddress::V4(192, 168, 2, 1), *IpAddress::Parse("2001:db8:2::1")}},
  };

  std::string v4_pkt = MakeIpv4Packet(IpAddress::V4(192, 168, 1, 10),
                                      IpAddress::V4(192, 168, 2, 20), 64, true, 32);
  ForwardingResult v4_disabled =
      EvaluatePacketForwarding(v4_pkt, 0, default_config, routes, interfaces);
  EXPECT(ForwardingAction::Drop, v4_disabled.action);
  EXPECT(ForwardingDropReason::ForwardingDisabled, v4_disabled.reason);

  std::string v6_pkt = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                      *IpAddress::Parse("2001:db8:2::20"), 64, 32);
  ForwardingResult v6_disabled =
      EvaluatePacketForwarding(v6_pkt, 0, default_config, routes, interfaces);
  EXPECT(ForwardingAction::Drop, v6_disabled.action);
  EXPECT(ForwardingDropReason::ForwardingDisabled, v6_disabled.reason);

  // Enable forwarding for both families.
  ForwardingConfig enabled{true, true};

  // Local destination is delivered locally, not forwarded.
  std::string local_v6 = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                        *IpAddress::Parse("2001:db8:2::1"), 64, 16);
  EXPECT(ForwardingAction::DeliverLocally,
         EvaluatePacketForwarding(local_v6, 0, enabled, routes, interfaces).action);

  // Link-local and multicast packets are never forwarded.
  std::string ll_v6 = MakeIpv6Packet(*IpAddress::Parse("fe80::10"),
                                     *IpAddress::Parse("2001:db8:2::20"), 64, 16);
  ForwardingResult ll_res =
      EvaluatePacketForwarding(ll_v6, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::Drop, ll_res.action);
  EXPECT(ForwardingDropReason::LinkLocalOrMulticast, ll_res.reason);

  std::string mcast_v6 = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                        *IpAddress::Parse("ff0e::1"), 64, 16);
  ForwardingResult mcast_res =
      EvaluatePacketForwarding(mcast_v6, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::Drop, mcast_res.action);
  EXPECT(ForwardingDropReason::LinkLocalOrMulticast, mcast_res.reason);

  std::string ll_v4 = MakeIpv4Packet(IpAddress::V4(169, 254, 1, 10),
                                     IpAddress::V4(192, 168, 2, 20), 64, false, 16);
  EXPECT(ForwardingDropReason::LinkLocalOrMulticast,
         EvaluatePacketForwarding(ll_v4, 0, enabled, routes, interfaces).reason);
}

TEST(ForwardingDecrementsTtlAndHopLimitAndGeneratesIcmpErrors) {
  RoutingTable routes;
  routes.AddConnectedRoute(IpAddress::V4(192, 168, 2, 0), 24, 1);
  routes.AddConnectedRoute(*IpAddress::Parse("2001:db8:2::"), 64, 1);

  std::vector<ForwardingInterface> interfaces = {
      {0, 1500, {IpAddress::V4(192, 168, 1, 1), *IpAddress::Parse("2001:db8:1::1")}},
      {1, 1400, {IpAddress::V4(192, 168, 2, 1), *IpAddress::Parse("2001:db8:2::1")}},
  };
  ForwardingConfig enabled{true, true};

  // Normal IPv4 forward: TTL decremented from 64 to 63 and checksum updated.
  std::string v4_ok = MakeIpv4Packet(IpAddress::V4(192, 168, 1, 10),
                                     IpAddress::V4(192, 168, 2, 20), 64, true, 100);
  ForwardingResult v4_fwd =
      EvaluatePacketForwarding(v4_ok, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::Forward, v4_fwd.action);
  EXPECT((size_t)1, v4_fwd.egress_interface_index);
  EXPECT(true, v4_fwd.next_hop == IpAddress::V4(192, 168, 2, 20));
  EXPECT((uint8)63, static_cast<uint8>(v4_fwd.packet[kIpv4TtlOffset]));
  EXPECT((uint16)0,
         Checksum(std::string_view(v4_fwd.packet).substr(0, kMinIpv4HeaderSize)));

  // Normal IPv6 forward: Hop Limit decremented from 64 to 63.
  std::string v6_ok = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                     *IpAddress::Parse("2001:db8:2::20"), 64, 100);
  ForwardingResult v6_fwd =
      EvaluatePacketForwarding(v6_ok, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::Forward, v6_fwd.action);
  EXPECT((size_t)1, v6_fwd.egress_interface_index);
  EXPECT(true, v6_fwd.next_hop == *IpAddress::Parse("2001:db8:2::20"));
  EXPECT((uint8)63, static_cast<uint8>(v6_fwd.packet[kIpv6HopLimitOffset]));

  // TTL <= 1 generates ICMPv4 Time Exceeded (Type 11, Code 0).
  std::string v4_ttl1 = MakeIpv4Packet(IpAddress::V4(192, 168, 1, 10),
                                       IpAddress::V4(192, 168, 2, 20), 1, true, 32);
  ForwardingResult v4_te =
      EvaluatePacketForwarding(v4_ttl1, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::SendIcmpError, v4_te.action);
  EXPECT(ForwardingDropReason::TimeExceeded, v4_te.reason);
  EXPECT((uint8)11, v4_te.icmp_type);
  EXPECT((uint8)0, v4_te.icmp_code);

  // Hop Limit <= 1 generates ICMPv6 Time Exceeded (Type 3, Code 0).
  std::string v6_hl1 = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                      *IpAddress::Parse("2001:db8:2::20"), 1, 32);
  ForwardingResult v6_te =
      EvaluatePacketForwarding(v6_hl1, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::SendIcmpError, v6_te.action);
  EXPECT(ForwardingDropReason::TimeExceeded, v6_te.reason);
  EXPECT((uint8)3, v6_te.icmp_type);
  EXPECT((uint8)0, v6_te.icmp_code);

  // Outgoing interface MTU (1400) exceeded on IPv6 -> ICMPv6 Packet Too Big (Type 2).
  std::string v6_big = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                      *IpAddress::Parse("2001:db8:2::20"), 64, 1400);
  ForwardingResult v6_ptb =
      EvaluatePacketForwarding(v6_big, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::SendIcmpError, v6_ptb.action);
  EXPECT(ForwardingDropReason::PacketTooBig, v6_ptb.reason);
  EXPECT((uint8)2, v6_ptb.icmp_type);
  EXPECT((uint16)1400, v6_ptb.reported_mtu);

  // Cached PMTU (1280) smaller than interface MTU (1400) triggers Packet Too Big.
  std::string v6_pmtu = MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                                       *IpAddress::Parse("2001:db8:2::20"), 64, 1300);
  ForwardingResult v6_pmtu_res = EvaluatePacketForwarding(
      v6_pmtu, 0, enabled, routes, interfaces,
      [](const IpAddress&) -> std::optional<uint16> { return 1280; });
  EXPECT(ForwardingAction::SendIcmpError, v6_pmtu_res.action);
  EXPECT((uint16)1280, v6_pmtu_res.reported_mtu);

  // Outgoing interface MTU (1400) exceeded on IPv4 with DF=1 -> ICMPv4 Frag Needed (3/4).
  std::string v4_big = MakeIpv4Packet(IpAddress::V4(192, 168, 1, 10),
                                      IpAddress::V4(192, 168, 2, 20), 64, true, 1400);
  ForwardingResult v4_ptb =
      EvaluatePacketForwarding(v4_big, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::SendIcmpError, v4_ptb.action);
  EXPECT(ForwardingDropReason::PacketTooBig, v4_ptb.reason);
  EXPECT((uint8)3, v4_ptb.icmp_type);
  EXPECT((uint8)4, v4_ptb.icmp_code);
  EXPECT((uint16)1400, v4_ptb.reported_mtu);

  // ICMPv6 error packet with Hop Limit 1 is dropped without generating another ICMPv6 error.
  std::string v6_icmp_err =
      MakeIpv6Packet(*IpAddress::Parse("2001:db8:1::10"),
                     *IpAddress::Parse("2001:db8:2::20"), 1, 16,
                     kProtocolIcmpv6, /*first_payload_byte=*/1);
  ForwardingResult v6_suppressed =
      EvaluatePacketForwarding(v6_icmp_err, 0, enabled, routes, interfaces);
  EXPECT(ForwardingAction::Drop, v6_suppressed.action);
}
