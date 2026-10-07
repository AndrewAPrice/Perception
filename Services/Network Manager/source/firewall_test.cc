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

#include "firewall.h"

#include <chrono>
#include <string>

#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// Protocol numbers.
constexpr uint8 kProtocolIcmpv4 = 1;
// TCP protocol number.
constexpr uint8 kProtocolTcp = 6;
// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;
// ICMPv6 protocol number.
constexpr uint8 kProtocolIcmpv6 = 58;

TEST(Firewall_DefaultDenyInboundAndStatefulOutboundReturn) {
  Firewall fw;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  IpAddress local_v6 = *IpAddress::Parse("fec0::5054:ff:fe12:3456");
  IpAddress remote_v6 = *IpAddress::Parse("2607:f8b0:4004:800::200e");

  // Unsolicited inbound TCP to port 80 is blocked by default.
  FirewallPacket inbound_syn;
  inbound_syn.direction = FirewallDirection::Inbound;
  inbound_syn.source = remote_v6;
  inbound_syn.destination = local_v6;
  inbound_syn.protocol = kProtocolTcp;
  inbound_syn.src_port = 54321;
  inbound_syn.dst_port = 80;
  EXPECT(FirewallAction::Deny, fw.Evaluate(inbound_syn, t0));

  // Outbound TCP connection from port 49152 to remote port 443 is allowed
  // by default and creates a state entry.
  FirewallPacket outbound_syn;
  outbound_syn.direction = FirewallDirection::Outbound;
  outbound_syn.source = local_v6;
  outbound_syn.destination = remote_v6;
  outbound_syn.protocol = kProtocolTcp;
  outbound_syn.src_port = 49152;
  outbound_syn.dst_port = 443;
  EXPECT(FirewallAction::Allow, fw.Evaluate(outbound_syn, t0));
  EXPECT(static_cast<size_t>(1), fw.StateCount());

  // Return inbound packet on the established 4-tuple is now allowed.
  FirewallPacket inbound_syn_ack;
  inbound_syn_ack.direction = FirewallDirection::Inbound;
  inbound_syn_ack.source = remote_v6;
  inbound_syn_ack.destination = local_v6;
  inbound_syn_ack.protocol = kProtocolTcp;
  inbound_syn_ack.src_port = 443;
  inbound_syn_ack.dst_port = 49152;
  EXPECT(FirewallAction::Allow,
         fw.Evaluate(inbound_syn_ack, t0 + std::chrono::seconds(1)));

  // After 300s of inactivity, the TCP state expires and inbound is denied.
  EXPECT(FirewallAction::Deny,
         fw.Evaluate(inbound_syn_ack, t0 + std::chrono::seconds(301)));
}

TEST(Firewall_IcmpEchoStateTracking) {
  Firewall fw;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  IpAddress local_v4 = IpAddress::V4(10, 0, 2, 15);
  IpAddress remote_v4 = IpAddress::V4(8, 8, 8, 8);

  // Unsolicited inbound Echo Request (type 8) is denied.
  FirewallPacket in_req;
  in_req.direction = FirewallDirection::Inbound;
  in_req.source = remote_v4;
  in_req.destination = local_v4;
  in_req.protocol = kProtocolIcmpv4;
  in_req.icmp_type = 8;
  in_req.icmp_identifier = 0x1234;
  EXPECT(FirewallAction::Deny, fw.Evaluate(in_req, t0));

  // Outbound Echo Request (type 8) is allowed and creates state for ID 0x1234.
  FirewallPacket out_req = in_req;
  out_req.direction = FirewallDirection::Outbound;
  out_req.source = local_v4;
  out_req.destination = remote_v4;
  EXPECT(FirewallAction::Allow, fw.Evaluate(out_req, t0));

  // Inbound Echo Request (type 8) from remote_v4 with the same ID still must
  // not match the outbound request's state entry.
  EXPECT(FirewallAction::Deny, fw.Evaluate(in_req, t0));

  // Inbound Echo Reply (type 0) with matching ID 0x1234 is allowed.
  FirewallPacket in_reply = in_req;
  in_reply.icmp_type = 0;
  EXPECT(FirewallAction::Allow, fw.Evaluate(in_reply, t0));
}

TEST(Firewall_EssentialIcmpv6AlwaysPermittedWithValidHopLimit) {
  Firewall fw;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  IpAddress router_ll = *IpAddress::Parse("fe80::2");
  IpAddress all_nodes = *IpAddress::Parse("ff02::1");
  IpAddress local_v6 = *IpAddress::Parse("fec0::5054:ff:fe12:3456");

  // Inbound NDP Router Advertisement (type 134) with hop limit 255 is allowed.
  FirewallPacket ra;
  ra.direction = FirewallDirection::Inbound;
  ra.source = router_ll;
  ra.destination = all_nodes;
  ra.protocol = kProtocolIcmpv6;
  ra.icmp_type = 134;
  ra.hop_limit = 255;
  EXPECT(FirewallAction::Allow, fw.Evaluate(ra, t0));

  // NDP with hop limit != 255 is rejected even if a rule would allow it.
  ra.hop_limit = 64;
  EXPECT(FirewallAction::Deny, fw.Evaluate(ra, t0));

  // MLD Query (type 130) with hop limit 1 and link-local source is allowed;
  // hop limit 255 is rejected.
  FirewallPacket mld;
  mld.direction = FirewallDirection::Inbound;
  mld.source = router_ll;
  mld.destination = all_nodes;
  mld.protocol = kProtocolIcmpv6;
  mld.icmp_type = 130;
  mld.hop_limit = 1;
  EXPECT(FirewallAction::Allow, fw.Evaluate(mld, t0));
  mld.hop_limit = 255;
  EXPECT(FirewallAction::Deny, fw.Evaluate(mld, t0));

  // ICMPv6 Packet Too Big (type 2) is allowed so PMTUD works.
  FirewallPacket ptb;
  ptb.direction = FirewallDirection::Inbound;
  ptb.source = *IpAddress::Parse("2001:4860:4860::1");
  ptb.destination = local_v6;
  ptb.protocol = kProtocolIcmpv6;
  ptb.icmp_type = 2;
  ptb.hop_limit = 58;
  EXPECT(FirewallAction::Allow, fw.Evaluate(ptb, t0));
}

TEST(Firewall_OrderedRulesAndFromPayload) {
  Firewall fw;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

  // Rule 0: Deny inbound TCP port 80 from 10.0.2.99/32 on interface 0.
  FirewallRule deny_bad_host;
  deny_bad_host.action = FirewallAction::Deny;
  deny_bad_host.direction = FirewallDirection::Inbound;
  deny_bad_host.family = IpAddressFamily::V4;
  deny_bad_host.protocol = kProtocolTcp;
  deny_bad_host.src_prefix = {IpAddress::V4(10, 0, 2, 99), 32};
  deny_bad_host.dst_port_range = {80, 80};
  deny_bad_host.interface_index = 0;
  fw.AddRule(deny_bad_host);

  // Rule 1: Allow inbound TCP ports 80..8080 from 10.0.2.0/24 on interface 0.
  FirewallRule allow_subnet_http;
  allow_subnet_http.action = FirewallAction::Allow;
  allow_subnet_http.direction = FirewallDirection::Inbound;
  allow_subnet_http.family = IpAddressFamily::V4;
  allow_subnet_http.protocol = kProtocolTcp;
  allow_subnet_http.src_prefix = {IpAddress::V4(10, 0, 2, 0), 24};
  allow_subnet_http.dst_port_range = {80, 8080};
  allow_subnet_http.interface_index = 0;
  fw.AddRule(allow_subnet_http);

  // Build raw TCP header snippet (src_port = 50000, dst_port = 80).
  std::string tcp_bytes;
  WireWriter writer(tcp_bytes);
  writer.WriteU16(50000);
  writer.WriteU16(80);

  // 10.0.2.99 hits Rule 0 (Deny).
  auto from_bad = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, IpAddress::V4(10, 0, 2, 99),
      IpAddress::V4(10, 0, 2, 15), kProtocolTcp, tcp_bytes, 64, 0);
  EXPECT(FirewallAction::Deny, fw.Evaluate(from_bad, t0));

  // 10.0.2.2 hits Rule 1 (Allow) on interface 0, but is denied on interface 1.
  auto from_good_if0 = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, IpAddress::V4(10, 0, 2, 2),
      IpAddress::V4(10, 0, 2, 15), kProtocolTcp, tcp_bytes, 64, 0);
  EXPECT(FirewallAction::Allow, fw.Evaluate(from_good_if0, t0));

  auto from_good_if1 = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, IpAddress::V4(10, 0, 2, 3),
      IpAddress::V4(10, 0, 2, 15), kProtocolTcp, tcp_bytes, 64, 1);
  EXPECT(FirewallAction::Deny, fw.Evaluate(from_good_if1, t0));

  // UDP packet to port 80 does not match TCP rule and is denied.
  auto udp_pkt = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, IpAddress::V4(10, 0, 2, 2),
      IpAddress::V4(10, 0, 2, 15), kProtocolUdp, tcp_bytes, 64, 0);
  EXPECT(FirewallAction::Deny, fw.Evaluate(udp_pkt, t0));
}

}  // namespace
