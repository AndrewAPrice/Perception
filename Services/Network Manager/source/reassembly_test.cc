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

#include "reassembly.h"

#include <chrono>
#include <string>

#include "testing.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;

// TCP protocol number.
constexpr uint8 kProtocolTcp = 6;

// ICMPv6 protocol number.
constexpr uint8 kProtocolIcmpv6 = 58;

// Builds a sample IPv4 reassembly key.
ReassemblyKey MakeV4Key(uint16 id = 100) {
  return {IpAddressFamily::V4, IpAddress::V4(10, 0, 2, 2),
          IpAddress::V4(10, 0, 2, 15), id, kProtocolUdp};
}

// Builds a sample IPv6 reassembly key.
ReassemblyKey MakeV6Key(uint32 id = 0x12345678, uint8 protocol = kProtocolUdp) {
  return {IpAddressFamily::V6, *IpAddress::Parse("2001:db8::1"),
          *IpAddress::Parse("2001:db8::2"), id, protocol};
}

TEST(Reassembly_InOrderAndOutOfOrderV4AndV6) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

  // In-order IPv4 reassembly of 3 fragments (8 + 8 + 5 = 21 bytes).
  ReassemblyKey v4_key = MakeV4Key(1);
  auto r1 = reassembler.AddFragment(v4_key, 0, true, "01234567", t0);
  EXPECT(ReassemblyStatus::Incomplete, r1.status);
  auto r2 = reassembler.AddFragment(v4_key, 8, true, "89abcdef", t0);
  EXPECT(ReassemblyStatus::Incomplete, r2.status);
  auto r3 = reassembler.AddFragment(v4_key, 16, false, "ghijk", t0);
  EXPECT(ReassemblyStatus::Complete, r3.status);
  EXPECT(std::string("0123456789abcdefghijk"), r3.payload);
  EXPECT(static_cast<size_t>(0), reassembler.ActiveDatagrams());

  // Out-of-order IPv6 reassembly (last, middle, first).
  ReassemblyKey v6_key = MakeV6Key(2, kProtocolUdp);
  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(v6_key, 16, false, "TER_DATA", t0).status);
  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(v6_key, 8, true, "UDP_LA", t0).status ==
             ReassemblyStatus::InvalidFragment
             ? ReassemblyStatus::Incomplete
             : ReassemblyStatus::Complete);
  // Valid 8-byte middle fragment.
  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(v6_key, 8, true, "MID_8BYT", t0).status);
  // First fragment >= 8 bytes (contains 8-byte UDP header).
  auto done = reassembler.AddFragment(v6_key, 0, true, "UDPHDR00", t0);
  EXPECT(ReassemblyStatus::Complete, done.status);
  EXPECT(kProtocolUdp, done.protocol);
  EXPECT(std::string("UDPHDR00MID_8BYTTER_DATA"), done.payload);
}

TEST(Reassembly_Rfc5722OverlapDropsWholeDatagram) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  ReassemblyKey key = MakeV6Key(10);

  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(key, 0, true, "0123456789abcdef", t0).status);
  EXPECT(static_cast<size_t>(1), reassembler.ActiveDatagrams());

  // Overlapping fragment at offset 8 drops the entire datagram.
  auto overlap = reassembler.AddFragment(key, 8, false, "overlap!", t0);
  EXPECT(ReassemblyStatus::DroppedOverlap, overlap.status);
  EXPECT(static_cast<size_t>(0), reassembler.ActiveDatagrams());

  // Sending a subsequent non-overlapping tail fragment now starts fresh and
  // cannot complete the dropped datagram.
  auto tail = reassembler.AddFragment(key, 16, false, "tail", t0);
  EXPECT(ReassemblyStatus::Incomplete, tail.status);
}

TEST(Reassembly_AtomicFragmentDoesNotDisturbInProgressDatagram) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  ReassemblyKey key = MakeV6Key(20);

  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(key, 0, true, "FIRST_FR", t0).status);
  EXPECT(static_cast<size_t>(1), reassembler.ActiveDatagrams());

  // Atomic fragment (offset=0, M=0) with the same key completes immediately
  // without discarding the in-progress reassembly buffer (RFC 6946).
  auto atomic = reassembler.AddFragment(key, 0, false, "ATOMIC_PAYLOAD", t0);
  EXPECT(ReassemblyStatus::Complete, atomic.status);
  EXPECT(std::string("ATOMIC_PAYLOAD"), atomic.payload);
  EXPECT(static_cast<size_t>(1), reassembler.ActiveDatagrams());

  auto second = reassembler.AddFragment(key, 8, false, "SECOND", t0);
  EXPECT(ReassemblyStatus::Complete, second.status);
  EXPECT(std::string("FIRST_FRSECOND"), second.payload);
}

TEST(Reassembly_TimeoutsAndPurgeMetadata) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  ReassemblyKey v4_key = MakeV4Key(30);
  ReassemblyKey v6_key = MakeV6Key(31);

  // V4 has fragment 0; V6 only has a non-zero fragment.
  reassembler.AddFragment(v4_key, 0, true, "V4_FIRST", t0);
  reassembler.AddFragment(v6_key, 8, false, "V6_TAIL", t0);

  // At t0 + 29s, neither has expired.
  EXPECT(true, reassembler.Purge(t0 + std::chrono::seconds(29)).empty());

  // At t0 + 30s, V4 (30s timeout) expires with had_first_fragment = true;
  // V6 (60s timeout) remains active.
  auto expired_v4 = reassembler.Purge(t0 + std::chrono::seconds(30));
  ASSERT(static_cast<size_t>(1), expired_v4.size());
  EXPECT(v4_key, expired_v4[0].key);
  EXPECT(true, expired_v4[0].had_first_fragment);
  EXPECT(std::string("V4_FIRST"), expired_v4[0].first_fragment_payload);
  EXPECT(static_cast<size_t>(1), reassembler.ActiveDatagrams());

  // At t0 + 60s, V6 expires with had_first_fragment = false.
  auto expired_v6 = reassembler.Purge(t0 + std::chrono::seconds(60));
  ASSERT(static_cast<size_t>(1), expired_v6.size());
  EXPECT(v6_key, expired_v6[0].key);
  EXPECT(false, expired_v6[0].had_first_fragment);
}

TEST(Reassembly_DatagramSizeAndConcurrencyCaps) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

  // Exceeding 65535 bytes is rejected.
  ReassemblyKey big_key = MakeV6Key(40);
  std::string sixteen_bytes(16, 'A');
  auto too_big = reassembler.AddFragment(big_key, 65528, false, sixteen_bytes, t0);
  EXPECT(ReassemblyStatus::DatagramTooLarge, too_big.status);

  // Inserting 33 concurrent datagrams caps active buffers at 32 by evicting
  // the earliest-expiring entry.
  for (uint32 i = 0; i < 33; i++) {
    reassembler.AddFragment(MakeV4Key(static_cast<uint16>(100 + i)), 0, true,
                            "01234567", t0 + std::chrono::milliseconds(i));
  }
  EXPECT(static_cast<size_t>(32), reassembler.ActiveDatagrams());
}

TEST(Reassembly_Rfc7112FirstFragmentAndRfc6980NdpChecks) {
  Reassembler reassembler;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

  // First TCP fragment with only 16 bytes (< 20-byte TCP header) is rejected
  // per RFC 7112.
  ReassemblyKey tcp_key = MakeV6Key(50, kProtocolTcp);
  std::string short_tcp(16, '\0');
  auto r_tcp = reassembler.AddFragment(tcp_key, 0, true, short_tcp, t0);
  EXPECT(ReassemblyStatus::IncompleteFirstFragmentHeader, r_tcp.status);

  // Valid 24-byte first TCP fragment with data offset = 5 (20-byte header)
  // is accepted.
  std::string valid_tcp(24, '\0');
  valid_tcp[12] = static_cast<char>(5 << 4);
  EXPECT(ReassemblyStatus::Incomplete,
         reassembler.AddFragment(tcp_key, 0, true, valid_tcp, t0).status);

  // Fragmented NDP Neighbor Solicitation (ICMPv6 type 135) is rejected per
  // RFC 6980, even in an atomic fragment.
  ReassemblyKey ndp_key = MakeV6Key(51, kProtocolIcmpv6);
  std::string ndp_ns(24, '\0');
  ndp_ns[0] = static_cast<char>(135);
  EXPECT(ReassemblyStatus::IncompleteFirstFragmentHeader,
         reassembler.AddFragment(ndp_key, 0, false, ndp_ns, t0).status);
  EXPECT(ReassemblyStatus::IncompleteFirstFragmentHeader,
         reassembler.AddFragment(ndp_key, 0, true, ndp_ns, t0).status);
}

}  // namespace
