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

#include "ipv6_header.h"

#include "icmpv6.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Sample traffic class used in header round-trip tests.
constexpr uint8 kTestTrafficClass = 0x28;

// Sample 20-bit flow label used in header round-trip tests.
constexpr uint32 kTestFlowLabel = 0xABCDE;

// Sample hop limit used in header round-trip tests.
constexpr uint8 kTestHopLimit = 64;

// Unknown Next Header protocol number for negative tests.
constexpr uint8 kUnknownProtocol = 250;

// Offset of the Next Header field in the fixed IPv6 header.
constexpr uint32 kFixedNextHeaderOffset = 6;

// Offset of the Payload Length field in the fixed IPv6 header.
constexpr uint32 kPayloadLengthOffset = 4;

// Parameter Problem code for erroneous header field.
constexpr uint8 kProblemErroneousField = 0;

// Parameter Problem code for unrecognized Next Header type.
constexpr uint8 kProblemUnknownNextHeader = 1;

// Parameter Problem code for unrecognized IPv6 option.
constexpr uint8 kProblemUnknownOption = 2;

IpAddress TestSourceAddress() {
  return *IpAddress::Parse("fe80::5054:ff:fe12:3456");
}

IpAddress TestDestinationAddress() {
  return *IpAddress::Parse("fec0::2");
}

std::string BuildPacketWithPayload(Ipv6Header header,
                                   std::string_view payload) {
  header.payload_length = static_cast<uint16>(payload.size());
  std::string packet;
  AppendIpv6Header(header, packet);
  packet.append(payload);
  return packet;
}

TEST(Ipv6Header_RoundTripAndPadding) {
  Ipv6Header header;
  header.traffic_class = kTestTrafficClass;
  header.flow_label = kTestFlowLabel;
  header.payload_length = 4;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::Udp);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  std::string packet;
  AppendIpv6Header(header, packet);
  packet.append("ping");
  // Append trailing Ethernet padding bytes beyond payload_length.
  packet.append(16, '\0');

  auto parsed = ParseIpv6Header(packet);
  ASSERT(true, parsed.has_value());
  EXPECT(kTestTrafficClass, parsed->traffic_class);
  EXPECT(kTestFlowLabel, parsed->flow_label);
  EXPECT(static_cast<uint16>(4), parsed->payload_length);
  EXPECT(static_cast<uint8>(Ipv6NextHeader::Udp), parsed->next_header);
  EXPECT(kTestHopLimit, parsed->hop_limit);
  EXPECT(header.source, parsed->source);
  EXPECT(header.destination, parsed->destination);

  ExtensionWalkResult walk = WalkExtensionHeaders(*parsed, packet);
  EXPECT(ExtensionWalkStatus::UpperLayer, walk.status);
  EXPECT(static_cast<uint8>(Ipv6NextHeader::Udp), walk.next_header);
  EXPECT(std::string_view("ping"), walk.payload);
}

TEST(Ipv6Header_RejectsShortOrWrongVersionOrTruncatedPayload) {
  std::string short_packet(kIpv6HeaderSize - 1, '\0');
  EXPECT(false, ParseIpv6Header(short_packet).has_value());

  Ipv6Header header;
  header.payload_length = 10;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::Tcp);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  std::string truncated;
  AppendIpv6Header(header, truncated);
  truncated.append("short");
  EXPECT(false, ParseIpv6Header(truncated).has_value());

  std::string wrong_version;
  AppendIpv6Header(header, wrong_version);
  wrong_version.append(10, 'x');
  wrong_version[0] = 0x40;
  EXPECT(false, ParseIpv6Header(wrong_version).has_value());
}

TEST(Ipv6Header_SerializeWithHopByHopRouterAlert) {
  Ipv6Datagram datagram;
  datagram.source = TestSourceAddress();
  datagram.destination = AllMldv2RoutersMulticastAddress();
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = kMldHopLimit;
  datagram.router_alert = true;
  datagram.payload = "mld-body";

  std::string raw = SerializeIpv6Datagram(datagram);
  auto parsed = ParseIpv6Header(raw);
  ASSERT(true, parsed.has_value());
  EXPECT(static_cast<uint8>(Ipv6NextHeader::HopByHop), parsed->next_header);

  ExtensionWalkResult walk = WalkExtensionHeaders(*parsed, raw);
  EXPECT(ExtensionWalkStatus::UpperLayer, walk.status);
  EXPECT(true, walk.router_alert);
  EXPECT(static_cast<uint16>(0), walk.router_alert_value);
  EXPECT(static_cast<uint8>(Ipv6NextHeader::Icmpv6), walk.next_header);
  EXPECT(std::string_view("mld-body"), walk.payload);
}

TEST(Ipv6Header_HopByHopNotFirstIsParameterProblem) {
  Ipv6Header header;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  std::string ext;
  WireWriter writer(ext);
  // Destination Options followed by Hop-by-Hop (illegal after the first header).
  writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::HopByHop));
  writer.WriteU8(0);
  writer.WriteU8(1);
  writer.WriteU8(4);
  writer.WriteZeros(4);
  // Hop-by-Hop header.
  writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Tcp));
  writer.WriteU8(0);
  writer.WriteZeros(6);

  std::string packet = BuildPacketWithPayload(header, ext);
  auto parsed = ParseIpv6Header(packet);
  ASSERT(true, parsed.has_value());
  ExtensionWalkResult walk = WalkExtensionHeaders(*parsed, packet);
  EXPECT(ExtensionWalkStatus::ParameterProblem, walk.status);
  EXPECT(kProblemUnknownNextHeader, walk.problem_code);
  EXPECT(static_cast<uint32>(kIpv6HeaderSize), walk.problem_pointer);
}

TEST(Ipv6Header_RoutingHeaderSegmentsLeftHandling) {
  Ipv6Header header;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::Routing);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  // Segments Left == 0 is accepted and skipped.
  std::string rh_zero;
  {
    WireWriter writer(rh_zero);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU8(0);
    writer.WriteU8(0);
    writer.WriteZeros(4);
    writer.WriteBytes("ok");
  }
  std::string pkt_ok = BuildPacketWithPayload(header, rh_zero);
  auto parsed_ok = ParseIpv6Header(pkt_ok);
  ASSERT(true, parsed_ok.has_value());
  ExtensionWalkResult walk_ok = WalkExtensionHeaders(*parsed_ok, pkt_ok);
  EXPECT(ExtensionWalkStatus::UpperLayer, walk_ok.status);
  EXPECT(std::string_view("ok"), walk_ok.payload);

  // Segments Left != 0 (e.g. Type 0 routing header) is rejected with Parameter
  // Problem code 0 pointing at the Routing Type byte (offset 42).
  std::string rh_nonzero;
  {
    WireWriter writer(rh_nonzero);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU8(0);
    writer.WriteU8(1);
    writer.WriteZeros(4);
  }
  std::string pkt_bad = BuildPacketWithPayload(header, rh_nonzero);
  auto parsed_bad = ParseIpv6Header(pkt_bad);
  ASSERT(true, parsed_bad.has_value());
  ExtensionWalkResult walk_bad = WalkExtensionHeaders(*parsed_bad, pkt_bad);
  EXPECT(ExtensionWalkStatus::ParameterProblem, walk_bad.status);
  EXPECT(kProblemErroneousField, walk_bad.problem_code);
  EXPECT(static_cast<uint32>(kIpv6HeaderSize + 2), walk_bad.problem_pointer);
}

TEST(Ipv6Header_UnknownNextHeaderAndNoNextHeader) {
  Ipv6Header header;
  header.next_header = kUnknownProtocol;
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  std::string pkt_unknown = BuildPacketWithPayload(header, "data");
  auto parsed_unknown = ParseIpv6Header(pkt_unknown);
  ASSERT(true, parsed_unknown.has_value());
  ExtensionWalkResult walk_unknown =
      WalkExtensionHeaders(*parsed_unknown, pkt_unknown);
  EXPECT(ExtensionWalkStatus::ParameterProblem, walk_unknown.status);
  EXPECT(kProblemUnknownNextHeader, walk_unknown.problem_code);
  EXPECT(kFixedNextHeaderOffset, walk_unknown.problem_pointer);

  header.next_header = static_cast<uint8>(Ipv6NextHeader::NoNextHeader);
  std::string pkt_none = BuildPacketWithPayload(header, "");
  auto parsed_none = ParseIpv6Header(pkt_none);
  ASSERT(true, parsed_none.has_value());
  ExtensionWalkResult walk_none = WalkExtensionHeaders(*parsed_none, pkt_none);
  EXPECT(ExtensionWalkStatus::NoNextHeader, walk_none.status);
}

TEST(Ipv6Header_DestinationOptionActionBits) {
  Ipv6Header header;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  auto build_opt_packet = [&](uint8 opt_type, const IpAddress& dst) {
    Ipv6Header h = header;
    h.destination = dst;
    std::string ext;
    WireWriter writer(ext);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU8(opt_type);
    writer.WriteU8(4);
    writer.WriteZeros(4);
    return BuildPacketWithPayload(h, ext);
  };

  // Action 00: skip option and continue.
  std::string pkt_00 = build_opt_packet(0x1E, TestDestinationAddress());
  EXPECT(ExtensionWalkStatus::UpperLayer,
         WalkExtensionHeaders(*ParseIpv6Header(pkt_00), pkt_00).status);

  // Action 01: discard silently.
  std::string pkt_01 = build_opt_packet(0x5E, TestDestinationAddress());
  EXPECT(ExtensionWalkStatus::Discard,
         WalkExtensionHeaders(*ParseIpv6Header(pkt_01), pkt_01).status);

  // Action 10: Parameter Problem code 2 regardless of multicast.
  std::string pkt_10 = build_opt_packet(0x9E, AllNodesMulticastAddress());
  ExtensionWalkResult walk_10 =
      WalkExtensionHeaders(*ParseIpv6Header(pkt_10), pkt_10);
  EXPECT(ExtensionWalkStatus::ParameterProblem, walk_10.status);
  EXPECT(kProblemUnknownOption, walk_10.problem_code);
  EXPECT(static_cast<uint32>(kIpv6HeaderSize + 2), walk_10.problem_pointer);

  // Action 11: Parameter Problem for unicast, Discard for multicast.
  std::string pkt_11_uni = build_opt_packet(0xDE, TestDestinationAddress());
  EXPECT(ExtensionWalkStatus::ParameterProblem,
         WalkExtensionHeaders(*ParseIpv6Header(pkt_11_uni), pkt_11_uni).status);

  std::string pkt_11_mcast = build_opt_packet(0xDE, AllNodesMulticastAddress());
  EXPECT(
      ExtensionWalkStatus::Discard,
      WalkExtensionHeaders(*ParseIpv6Header(pkt_11_mcast), pkt_11_mcast).status);
}

TEST(Ipv6Header_FragmentAtomicAndNonAtomicAndValidation) {
  Ipv6Header header;
  header.next_header = static_cast<uint8>(Ipv6NextHeader::Fragment);
  header.hop_limit = kTestHopLimit;
  header.source = TestSourceAddress();
  header.destination = TestDestinationAddress();

  // Atomic fragment (offset 0, M=0) is processed directly as UpperLayer.
  std::string atomic_ext;
  {
    WireWriter writer(atomic_ext);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU16(0);
    writer.WriteU32(0x12345678);
    writer.WriteBytes("hello");
  }
  std::string atomic_pkt = BuildPacketWithPayload(header, atomic_ext);
  ExtensionWalkResult atomic_walk =
      WalkExtensionHeaders(*ParseIpv6Header(atomic_pkt), atomic_pkt);
  EXPECT(ExtensionWalkStatus::UpperLayer, atomic_walk.status);
  ASSERT(true, atomic_walk.fragment.has_value());
  EXPECT(static_cast<uint32>(0x12345678), atomic_walk.fragment->identification);
  EXPECT(std::string_view("hello"), atomic_walk.payload);

  // Non-atomic first fragment with 8-byte payload and M=1.
  std::string frag_ext;
  {
    WireWriter writer(frag_ext);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU16(0x0001);
    writer.WriteU32(0xCAFEBABE);
    writer.WriteBytes("12345678");
  }
  std::string frag_pkt = BuildPacketWithPayload(header, frag_ext);
  ExtensionWalkResult frag_walk =
      WalkExtensionHeaders(*ParseIpv6Header(frag_pkt), frag_pkt);
  EXPECT(ExtensionWalkStatus::Fragment, frag_walk.status);
  ASSERT(true, frag_walk.fragment.has_value());
  EXPECT(true, frag_walk.fragment->more_fragments);
  EXPECT(static_cast<uint16>(0), frag_walk.fragment->offset);
  EXPECT(std::string_view("12345678"), frag_walk.payload);

  // Non-final fragment whose length is not a multiple of 8 -> Parameter Problem
  // pointing at Payload Length (offset 4).
  std::string unaligned_ext;
  {
    WireWriter writer(unaligned_ext);
    writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Udp));
    writer.WriteU8(0);
    writer.WriteU16(0x0001);
    writer.WriteU32(1);
    writer.WriteBytes("12345");
  }
  std::string unaligned_pkt = BuildPacketWithPayload(header, unaligned_ext);
  ExtensionWalkResult unaligned_walk =
      WalkExtensionHeaders(*ParseIpv6Header(unaligned_pkt), unaligned_pkt);
  EXPECT(ExtensionWalkStatus::ParameterProblem, unaligned_walk.status);
  EXPECT(kPayloadLengthOffset, unaligned_walk.problem_pointer);
}

TEST(Ipv6Header_AddressHelpers) {
  IpAddress addr = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  EXPECT(*IpAddress::Parse("ff02::1:ff12:3456"),
         SolicitedNodeMulticastAddress(addr));

  IpAddress prefix = *IpAddress::Parse("fec0::");
  std::array<uint8, 8> iid = {0x50, 0x54, 0x00, 0xff, 0xfe, 0x12, 0x34, 0x56};
  IpAddress combined = CombinePrefixAndInterfaceIdentifier(prefix, iid);
  EXPECT(*IpAddress::Parse("fec0::5054:ff:fe12:3456"), combined);
  EXPECT(true, PrefixMatches(prefix, combined, 64));
  EXPECT(false, PrefixMatches(prefix, addr, 64));
}

TEST(Icmpv6_EchoRequestAndReplyCodec) {
  IpAddress src = TestSourceAddress();
  IpAddress dst = TestDestinationAddress();

  Icmpv6EchoMessage req;
  req.is_reply = false;
  req.identifier = 0x1234;
  req.sequence = 7;
  req.data = "echo-payload";

  std::string raw_req = BuildIcmpv6Echo(src, dst, req);
  EXPECT(true, VerifyIcmpv6Checksum(src, dst, raw_req));

  auto parsed_req = ParseIcmpv6Echo(raw_req);
  ASSERT(true, parsed_req.has_value());
  EXPECT(false, parsed_req->is_reply);
  EXPECT(static_cast<uint16>(0x1234), parsed_req->identifier);
  EXPECT(static_cast<uint16>(7), parsed_req->sequence);
  EXPECT(std::string("echo-payload"), parsed_req->data);

  Ipv6Datagram reply_dgram = MakeIcmpv6EchoReply(dst, src, kTestHopLimit, *parsed_req);
  EXPECT(dst, reply_dgram.source);
  EXPECT(src, reply_dgram.destination);
  EXPECT(true, VerifyIcmpv6Checksum(dst, src, reply_dgram.payload));
  auto parsed_reply = ParseIcmpv6Echo(reply_dgram.payload);
  ASSERT(true, parsed_reply.has_value());
  EXPECT(true, parsed_reply->is_reply);
  EXPECT(static_cast<uint16>(0x1234), parsed_reply->identifier);
  EXPECT(static_cast<uint16>(7), parsed_reply->sequence);
  EXPECT(std::string("echo-payload"), parsed_reply->data);
}

TEST(Icmpv6_ErrorCodecAndSuppressionRules) {
  IpAddress src = TestSourceAddress();
  IpAddress dst = TestDestinationAddress();

  Ipv6Header invoking_hdr;
  invoking_hdr.next_header = static_cast<uint8>(Ipv6NextHeader::Udp);
  invoking_hdr.hop_limit = kTestHopLimit;
  invoking_hdr.source = dst;
  invoking_hdr.destination = src;
  std::string invoking_pkt = BuildPacketWithPayload(invoking_hdr, "udpdata");

  auto ptb = BuildIcmpv6Error(src, dst, Icmpv6Type::PacketTooBig, 0, 1280,
                              invoking_pkt);
  ASSERT(true, ptb.has_value());
  EXPECT(true, VerifyIcmpv6Checksum(src, dst, *ptb));
  auto parsed_err = ParseIcmpv6Error(*ptb);
  ASSERT(true, parsed_err.has_value());
  EXPECT(Icmpv6Type::PacketTooBig, parsed_err->type);
  EXPECT(static_cast<uint32>(1280), parsed_err->parameter);
  ASSERT(true, parsed_err->invoking_header.has_value());
  EXPECT(dst, parsed_err->invoking_header->source);

  // Never send an error in response to another ICMPv6 error.
  Ipv6Header err_invoking_hdr = invoking_hdr;
  err_invoking_hdr.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  std::string err_invoking_pkt =
      BuildPacketWithPayload(err_invoking_hdr, *ptb);
  EXPECT(false,
         BuildIcmpv6Error(src, dst, Icmpv6Type::DestinationUnreachable, 0, 0,
                          err_invoking_pkt)
             .has_value());

  // Multicast destination suppresses Destination Unreachable, but allows
  // Packet Too Big and Parameter Problem code 2.
  Ipv6Header mcast_hdr = invoking_hdr;
  mcast_hdr.destination = AllNodesMulticastAddress();
  std::string mcast_pkt = BuildPacketWithPayload(mcast_hdr, "mcast");
  EXPECT(false,
         BuildIcmpv6Error(src, dst, Icmpv6Type::DestinationUnreachable, 0, 0,
                          mcast_pkt)
             .has_value());
  EXPECT(true,
         BuildIcmpv6Error(src, dst, Icmpv6Type::PacketTooBig, 0, 1280,
                          mcast_pkt)
             .has_value());
  EXPECT(true,
         BuildIcmpv6Error(
             src, dst, Icmpv6Type::ParameterProblem,
             static_cast<uint8>(Icmpv6ParameterProblemCode::UnrecognizedIpv6Option),
             42, mcast_pkt)
             .has_value());
}

TEST(Icmpv6_TokenBucketRateLimiter) {
  Icmpv6RateLimiter limiter;
  auto t0 = std::chrono::steady_clock::time_point{};
  for (int i = 0; i < 10; i++)
    EXPECT(true, limiter.Allow(t0));
  EXPECT(false, limiter.Allow(t0));
  EXPECT(true, limiter.Allow(t0 + std::chrono::milliseconds(100)));
  EXPECT(false, limiter.Allow(t0 + std::chrono::milliseconds(100)));
}

}  // namespace

