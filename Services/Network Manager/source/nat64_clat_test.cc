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

#include <array>
#include <string>
#include <vector>

#include "checksum.h"
#include "fragmentation.h"
#include "ipv6_header.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Protocol numbers.
constexpr uint8 kProtocolIcmpv4 = 1;
// TCP protocol number.
constexpr uint8 kProtocolTcp = 6;
// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;
// ICMPv6 protocol number.
constexpr uint8 kProtocolIcmpv6 = 58;

TEST(Nat64Clat_Pref64RaOptionEncodeAndParse) {
  Nat64Prefix wk = WellKnownNat64Prefix();
  wk.lifetime_seconds = 600;

  auto encoded = EncodePref64RaOption(wk);
  ASSERT(true, encoded.has_value());
  EXPECT(static_cast<size_t>(16), encoded->size());

  auto parsed = ParsePref64RaOption(*encoded);
  ASSERT(true, parsed.has_value());
  EXPECT(*IpAddress::Parse("64:ff9b::"), parsed->prefix);
  EXPECT(static_cast<uint8>(96), parsed->prefix_length);
  EXPECT(static_cast<uint32>(600), parsed->lifetime_seconds);

  // /64 prefix (PLC = 1) round-trip.
  Nat64Prefix p64{*IpAddress::Parse("2001:db8:64::"), 64, 1800};
  auto enc64 = EncodePref64RaOption(p64);
  ASSERT(true, enc64.has_value());
  auto dec64 = ParsePref64RaOption(*enc64);
  ASSERT(true, dec64.has_value());
  EXPECT(p64, *dec64);

  // Reserved PLC = 7 is rejected.
  std::string bad_plc = *encoded;
  bad_plc[3] = static_cast<char>((bad_plc[3] & ~0x07) | 7);
  EXPECT(false, ParsePref64RaOption(bad_plc).has_value());
}

TEST(Nat64Clat_Rfc6052AllPrefixLengthsRoundTrip) {
  IpAddress v4 = IpAddress::V4(192, 0, 2, 33);
  IpAddress base = *IpAddress::Parse("2001:db8:1234:5678:9abc:def0::");

  for (uint8 len : std::array<uint8, 6>{96, 64, 56, 48, 40, 32}) {
    Nat64Prefix p{base, len, 0};
    auto enc = EncodePref64RaOption(p);
    ASSERT(true, enc.has_value());
    Nat64Prefix masked = *ParsePref64RaOption(*enc);

    auto v6 = SynthesizeIpv6FromIpv4(masked, v4);
    ASSERT(true, v6.has_value());
    if (len < 96) EXPECT(static_cast<uint8>(0), v6->bytes()[8]);

    auto extracted = ExtractIpv4FromSynthesizedIpv6(masked, *v6);
    ASSERT(true, extracted.has_value());
    EXPECT(v4, *extracted);
  }
}

TEST(Nat64Clat_Rfc7050DiscoveryAndDns64Synthesis) {
  EXPECT(std::string_view("ipv4only.arpa"), kIpv4OnlyArpaDomain);

  // Discover 64:ff9b::/96 from synthesized 192.0.0.170 / 192.0.0.171 answers.
  IpAddress ans1 = *IpAddress::Parse("64:ff9b::192.0.0.170");
  IpAddress ans2 = *IpAddress::Parse("64:ff9b::192.0.0.171");
  auto discovered =
      DiscoverNat64PrefixFromDns64Answers(std::array{ans1, ans2});
  ASSERT(true, discovered.has_value());
  EXPECT(*IpAddress::Parse("64:ff9b::"), discovered->prefix);
  EXPECT(static_cast<uint8>(96), discovered->prefix_length);

  // Discover a /64 NAT64 prefix (2001:db8:abcd:ef01::/64).
  Nat64Prefix custom64{*IpAddress::Parse("2001:db8:abcd:ef01::"), 64, 0};
  IpAddress custom_ans =
      *SynthesizeIpv6FromIpv4(custom64, IpAddress::V4(192, 0, 0, 170));
  auto disc64 = DiscoverNat64PrefixFromDns64Answers(std::array{custom_ans});
  ASSERT(true, disc64.has_value());
  EXPECT(custom64.prefix, disc64->prefix);
  EXPECT(static_cast<uint8>(64), disc64->prefix_length);

  // Local DNS64 synthesis (RFC 6147): synthesizes when AAAA is empty, and
  // keeps native AAAA when non-empty.
  std::vector<DnsAddressRecord> a_records = {
      {IpAddress::V4(93, 184, 216, 34), 300},
  };
  auto synth = SynthesizeDns64Records(*discovered, {}, a_records);
  ASSERT(static_cast<size_t>(1), synth.size());
  EXPECT(*IpAddress::Parse("64:ff9b::93.184.216.34"), synth[0].address);
  EXPECT(static_cast<uint32>(300), synth[0].ttl);

  std::vector<DnsAddressRecord> native_aaaa = {
      {*IpAddress::Parse("2606:2800:220:1::248"), 120},
  };
  auto kept = SynthesizeDns64Records(*discovered, native_aaaa, a_records);
  ASSERT(static_cast<size_t>(1), kept.size());
  EXPECT(native_aaaa[0].address, kept[0].address);
}

TEST(Nat64Clat_464XlatUdpTcpAndIcmpRoundTrip) {
  Nat64Prefix nat64 = WellKnownNat64Prefix();
  IpAddress clat_v4 = ClatLocalIpv4Address();
  EXPECT(IpAddress::V4(192, 0, 0, 4), clat_v4);
  IpAddress clat_v6 = *IpAddress::Parse("fec0::464");
  IpAddress remote_v4 = IpAddress::V4(93, 184, 216, 34);
  IpAddress remote_v6 = *SynthesizeIpv6FromIpv4(nat64, remote_v4);

  // Build an outgoing UDP/IPv4 packet from 192.0.0.4:53000 to 93.184.216.34:80.
  std::string udp_payload;
  WireWriter udp_writer(udp_payload);
  udp_writer.WriteU16(53000);
  udp_writer.WriteU16(80);
  udp_writer.WriteU16(13);  // 8-byte header + "hello".
  udp_writer.WriteU16(0);
  udp_writer.WriteBytes("hello");

  Ipv4Datagram v4_udp;
  v4_udp.source = clat_v4;
  v4_udp.destination = remote_v4;
  v4_udp.protocol = kProtocolUdp;
  v4_udp.ttl = 64;
  v4_udp.payload = udp_payload;
  auto v4_packets = FragmentIpv4Datagram(v4_udp, 1500, 0x1111);
  ASSERT(true, v4_packets.has_value());

  // Translate IPv4 -> IPv6 via CLAT.
  auto translated_v6 = TranslateIpv4ToIpv6((*v4_packets)[0], clat_v6, nat64);
  ASSERT(true, translated_v6.has_value());
  auto v6_hdr = ParseIpv6Header(*translated_v6);
  ASSERT(true, v6_hdr.has_value());
  EXPECT(clat_v6, v6_hdr->source);
  EXPECT(remote_v6, v6_hdr->destination);
  EXPECT(kProtocolUdp, v6_hdr->next_header);

  // Simulate the remote server replying from remote_v6 to clat_v6, and
  // translate IPv6 -> IPv4 via CLAT.
  Ipv6Datagram reply_v6;
  reply_v6.source = remote_v6;
  reply_v6.destination = clat_v6;
  reply_v6.next_header = kProtocolUdp;
  reply_v6.hop_limit = 55;
  reply_v6.payload = translated_v6->substr(kIpv6HeaderSize);
  std::string raw_reply_v6 = SerializeIpv6Datagram(reply_v6);

  auto translated_v4 =
      TranslateIpv6ToIpv4(raw_reply_v6, clat_v6, nat64, clat_v4);
  ASSERT(true, translated_v4.has_value());
  EXPECT(static_cast<uint16>(0),
         InternetChecksum(std::string_view(translated_v4->data(), 20)));
  EXPECT(std::string_view("hello"), translated_v4->substr(28));

  // Translate ICMPv4 Echo Request (type 8) -> ICMPv6 Echo Request (type 128).
  std::string icmp4_echo;
  WireWriter icmp_writer(icmp4_echo);
  icmp_writer.WriteU8(8);  // Echo Request.
  icmp_writer.WriteU8(0);
  icmp_writer.WriteU16(0);
  icmp_writer.WriteU16(0x4242);  // Identifier.
  icmp_writer.WriteU16(1);       // Sequence.
  icmp_writer.WriteBytes("ping");

  Ipv4Datagram v4_icmp;
  v4_icmp.source = clat_v4;
  v4_icmp.destination = remote_v4;
  v4_icmp.protocol = kProtocolIcmpv4;
  v4_icmp.ttl = 64;
  v4_icmp.payload = icmp4_echo;
  auto v4_icmp_pkts = FragmentIpv4Datagram(v4_icmp, 1500, 0x2222);
  ASSERT(true, v4_icmp_pkts.has_value());

  auto v6_icmp = TranslateIpv4ToIpv6((*v4_icmp_pkts)[0], clat_v6, nat64);
  ASSERT(true, v6_icmp.has_value());
  auto v6_icmp_hdr = ParseIpv6Header(*v6_icmp);
  ASSERT(true, v6_icmp_hdr.has_value());
  EXPECT(kProtocolIcmpv6, v6_icmp_hdr->next_header);
  EXPECT(static_cast<uint8>(128),
         static_cast<uint8>((*v6_icmp)[kIpv6HeaderSize]));

  // Translate an incoming ICMPv6 Echo Reply (type 129) -> ICMPv4 Echo Reply (0).
  Ipv6Datagram v6_echo_reply;
  v6_echo_reply.source = remote_v6;
  v6_echo_reply.destination = clat_v6;
  v6_echo_reply.next_header = kProtocolIcmpv6;
  v6_echo_reply.hop_limit = 60;
  v6_echo_reply.payload = v6_icmp->substr(kIpv6HeaderSize);
  v6_echo_reply.payload[0] = static_cast<char>(129);  // Echo Reply.
  std::string raw_v6_echo_reply = SerializeIpv6Datagram(v6_echo_reply);

  auto v4_echo_reply =
      TranslateIpv6ToIpv4(raw_v6_echo_reply, clat_v6, nat64, clat_v4);
  ASSERT(true, v4_echo_reply.has_value());
  EXPECT(static_cast<uint8>(kProtocolIcmpv4),
         static_cast<uint8>((*v4_echo_reply)[9]));
  EXPECT(static_cast<uint8>(0), static_cast<uint8>((*v4_echo_reply)[20]));
  EXPECT(static_cast<uint16>(0),
         InternetChecksum(std::string_view(*v4_echo_reply).substr(20)));

  // Translate a TCP/IPv4 segment -> TCP/IPv6 -> TCP/IPv4.
  std::string tcp_seg(20, '\0');
  WireWriter tcp_writer(tcp_seg);
  tcp_writer.PatchU16(0, 49152);
  tcp_writer.PatchU16(2, 443);
  tcp_seg[12] = static_cast<char>(5 << 4);
  Ipv4Datagram v4_tcp;
  v4_tcp.source = clat_v4;
  v4_tcp.destination = remote_v4;
  v4_tcp.protocol = kProtocolTcp;
  v4_tcp.ttl = 64;
  v4_tcp.payload = tcp_seg;
  auto v4_tcp_pkts = FragmentIpv4Datagram(v4_tcp, 1500, 0x3333);
  ASSERT(true, v4_tcp_pkts.has_value());
  auto v6_tcp = TranslateIpv4ToIpv6((*v4_tcp_pkts)[0], clat_v6, nat64);
  ASSERT(true, v6_tcp.has_value());
  auto v6_tcp_hdr = ParseIpv6Header(*v6_tcp);
  ASSERT(true, v6_tcp_hdr.has_value());
  EXPECT(kProtocolTcp, v6_tcp_hdr->next_header);
}

}  // namespace
