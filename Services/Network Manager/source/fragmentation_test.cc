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

#include "fragmentation.h"

#include <chrono>
#include <string>

#include "checksum.h"
#include "reassembly.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;

TEST(Fragmentation_PlanFragments8ByteAlignment) {
  std::string payload = "0123456789abcdefghijk";  // 21 bytes.

  // Fits in a single packet when max_fragment_payload >= 21.
  auto single = PlanFragments(payload, 21);
  ASSERT(true, single.has_value());
  ASSERT(static_cast<size_t>(1), single->size());
  EXPECT(static_cast<uint16>(0), (*single)[0].offset);
  EXPECT(false, (*single)[0].more_fragments);
  EXPECT(std::string_view(payload), (*single)[0].data);

  // With max_fragment_payload = 12, non-final slices round down to 8 bytes:
  // [0..8) M=1, [8..16) M=1, [16..21) M=0 (5 bytes, fits in 12).
  auto slices = PlanFragments(payload, 12);
  ASSERT(true, slices.has_value());
  ASSERT(static_cast<size_t>(3), slices->size());
  EXPECT(static_cast<uint16>(0), (*slices)[0].offset);
  EXPECT(true, (*slices)[0].more_fragments);
  EXPECT(std::string_view("01234567"), (*slices)[0].data);
  EXPECT(static_cast<uint16>(8), (*slices)[1].offset);
  EXPECT(true, (*slices)[1].more_fragments);
  EXPECT(std::string_view("89abcdef"), (*slices)[1].data);
  EXPECT(static_cast<uint16>(16), (*slices)[2].offset);
  EXPECT(false, (*slices)[2].more_fragments);
  EXPECT(std::string_view("ghijk"), (*slices)[2].data);

  // With max_fragment_payload = 13, after the first 8-byte aligned slice, the
  // remaining 13 bytes fit in the final fragment (which does not require
  // 8-byte alignment): [0..8) M=1, [8..21) M=0.
  auto two_slices = PlanFragments(payload, 13);
  ASSERT(true, two_slices.has_value());
  ASSERT(static_cast<size_t>(2), two_slices->size());
  EXPECT(static_cast<size_t>(8), (*two_slices)[0].data.size());
  EXPECT(static_cast<size_t>(13), (*two_slices)[1].data.size());
  EXPECT(false, (*two_slices)[1].more_fragments);

  // max_fragment_payload < 8 cannot hold an aligned non-final fragment.
  EXPECT(false, PlanFragments(payload, 7).has_value());
}

TEST(Fragmentation_Rfc7739PerDestinationIdGenerator) {
  uint32 next_seed = 0x1000;
  FragmentIdGenerator gen([&]() -> uint32 {
    next_seed += 0x1000;
    return next_seed;
  });

  IpAddress dst_a = *IpAddress::Parse("2001:db8::1");
  IpAddress dst_b = *IpAddress::Parse("2001:db8::2");

  // First call for dst_a seeds at 0x2000; second increments to 0x2001.
  EXPECT(static_cast<uint32>(0x2000), gen.NextIpv6Id(dst_a));
  EXPECT(static_cast<uint32>(0x2001), gen.NextIpv6Id(dst_a));

  // dst_b gets its own independent counter seeded at 0x3000.
  EXPECT(static_cast<uint32>(0x3000), gen.NextIpv6Id(dst_b));
  EXPECT(static_cast<uint32>(0x2002), gen.NextIpv6Id(dst_a));

  // Wraparound skips 0.
  FragmentIdGenerator wrap_gen([]() -> uint32 { return 0xFFFFFFFFu; });
  EXPECT(static_cast<uint32>(0xFFFFFFFFu), wrap_gen.NextIpv6Id(dst_a));
  EXPECT(static_cast<uint32>(1u), wrap_gen.NextIpv6Id(dst_a));
}

TEST(Fragmentation_Ipv6FragmentationAndReassemblyRoundTrip) {
  Ipv6Datagram dgram;
  dgram.source = *IpAddress::Parse("fec0::1");
  dgram.destination = *IpAddress::Parse("2001:4860:4860::8888");
  dgram.next_header = kProtocolUdp;
  dgram.hop_limit = 64;
  dgram.payload.resize(2500);
  for (size_t i = 0; i < dgram.payload.size(); i++)
    dgram.payload[i] = static_cast<char>('A' + (i % 26));

  // At MTU 1280, available payload per fragment is 1280 - 40 - 8 = 1232
  // (already a multiple of 8), so 2500 bytes splits into 1232 + 1232 + 36.
  auto fragments = FragmentIpv6Datagram(dgram, 1280, 0xCAFEBABEu);
  ASSERT(true, fragments.has_value());
  ASSERT(static_cast<size_t>(3), fragments->size());

  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
  ReassemblyResult last_result;

  for (size_t i = 0; i < fragments->size(); i++) {
    const std::string& packet = (*fragments)[i];
    EXPECT(true, packet.size() <= 1280);
    auto hdr = ParseIpv6Header(packet);
    ASSERT(true, hdr.has_value());
    auto walk = WalkExtensionHeaders(*hdr, packet);
    ASSERT(ExtensionWalkStatus::Fragment, walk.status);
    ASSERT(true, walk.fragment.has_value());
    EXPECT(static_cast<uint32>(0xCAFEBABEu), walk.fragment->identification);

    ReassemblyKey key{IpAddressFamily::V6, hdr->source, hdr->destination,
                      walk.fragment->identification, walk.next_header};
    last_result = reassembler.AddFragment(key, walk.fragment->offset,
                                          walk.fragment->more_fragments,
                                          walk.payload, t0);
    if (i + 1 < fragments->size())
      EXPECT(ReassemblyStatus::Incomplete, last_result.status);
  }

  EXPECT(ReassemblyStatus::Complete, last_result.status);
  EXPECT(kProtocolUdp, last_result.protocol);
  EXPECT(dgram.payload, last_result.payload);
}

TEST(Fragmentation_Ipv4FragmentationAndDontFragment) {
  Ipv4Datagram dgram;
  dgram.source = IpAddress::V4(10, 0, 2, 15);
  dgram.destination = IpAddress::V4(8, 8, 8, 8);
  dgram.protocol = kProtocolUdp;
  dgram.ttl = 64;
  dgram.payload = "0123456789abcdefghijk";  // 21 bytes.

  // With DF=1 and MTU=28 (20-byte header + 8-byte payload), fragmentation
  // is disallowed and returns nullopt.
  dgram.dont_fragment = true;
  EXPECT(false, FragmentIpv4Datagram(dgram, 28, 0x1234).has_value());

  // With DF=0 and MTU=28, emits 3 fragments (8 + 8 + 5 bytes) with valid
  // IPv4 header checksums.
  dgram.dont_fragment = false;
  auto packets = FragmentIpv4Datagram(dgram, 28, 0x1234);
  ASSERT(true, packets.has_value());
  ASSERT(static_cast<size_t>(3), packets->size());

  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1));
  ReassemblyResult result;
  for (const std::string& pkt : *packets) {
    ASSERT(true, pkt.size() >= 20);
    EXPECT(static_cast<uint16>(0),
           InternetChecksum(std::string_view(pkt.data(), 20)));
    WireReader reader(pkt);
    reader.Skip(4);
    uint16 id = reader.ReadU16();
    uint16 flags_offset = reader.ReadU16();
    bool mf = (flags_offset & 0x2000) != 0;
    uint16 offset = static_cast<uint16>((flags_offset & 0x1FFF) * 8);
    ReassemblyKey key{IpAddressFamily::V4, dgram.source, dgram.destination, id,
                      dgram.protocol};
    result = reassembler.AddFragment(key, offset, mf, pkt.substr(20), t0);
  }
  EXPECT(ReassemblyStatus::Complete, result.status);
  EXPECT(dgram.payload, result.payload);
}

}  // namespace
