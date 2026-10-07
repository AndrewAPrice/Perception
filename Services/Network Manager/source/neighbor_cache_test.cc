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

#include "neighbor_cache.h"

#include "ethernet.h"
#include "testing.h"

namespace {

using ::perception::network::IpAddress;

// Sample unicast MAC for testing.
constexpr std::array<uint8, 6> kSampleMac1 = {0x52, 0x55, 0x0A,
                                              0x00, 0x02, 0x02};

// Second sample unicast MAC for testing.
constexpr std::array<uint8, 6> kSampleMac2 = {0x52, 0x55, 0x0A,
                                              0x00, 0x02, 0x03};

// Sample source MAC for Ethernet tests.
constexpr std::array<uint8, 6> kLocalMac = {0x52, 0x54, 0x00,
                                            0x12, 0x34, 0x56};

TEST(NeighborCache_IncompleteToReachableDrainsWaitersAndPendingPackets) {
  NeighborCache cache;
  IpAddress peer = IpAddress::V4(10, 0, 2, 2);
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(10));

  EXPECT(false, cache.LookupMac(peer, t0).has_value());

  auto* fake_fiber_1 = reinterpret_cast<::perception::Fiber*>(0x1001);
  auto* fake_fiber_2 = reinterpret_cast<::perception::Fiber*>(0x1002);
  cache.AddWaiter(peer, fake_fiber_1, t0);
  cache.AddWaiter(peer, fake_fiber_2, t0);
  cache.EnqueuePendingPacket(peer, "pkt1", t0);
  cache.EnqueuePendingPacket(peer, "pkt2", t0);

  EXPECT(false, cache.LookupMac(peer, t0).has_value());

  auto result = cache.UpdateReachable(peer, kSampleMac1, t0);
  EXPECT(true, result.mac == kSampleMac1);
  EXPECT(static_cast<size_t>(2), result.waiters_to_wake.size());
  EXPECT(static_cast<size_t>(2), result.pending_packets.size());
  EXPECT(std::string("pkt1"), result.pending_packets[0]);
  EXPECT(std::string("pkt2"), result.pending_packets[1]);

  auto mac = cache.LookupMac(peer, t0);
  ASSERT(true, mac.has_value());
  EXPECT(true, *mac == kSampleMac1);
}

TEST(NeighborCache_PendingPacketQueueCappedAtThree) {
  NeighborCache cache;
  IpAddress peer = *IpAddress::Parse("fe80::2");
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1));

  cache.EnqueuePendingPacket(peer, "p1", t0);
  cache.EnqueuePendingPacket(peer, "p2", t0);
  cache.EnqueuePendingPacket(peer, "p3", t0);
  cache.EnqueuePendingPacket(peer, "p4", t0);

  auto result = cache.UpdateReachable(peer, kSampleMac1, t0);
  ASSERT(static_cast<size_t>(3), result.pending_packets.size());
  EXPECT(std::string("p2"), result.pending_packets[0]);
  EXPECT(std::string("p3"), result.pending_packets[1]);
  EXPECT(std::string("p4"), result.pending_packets[2]);
}

TEST(NeighborCache_LruEvictionCapsAt256Entries) {
  NeighborCache cache;
  auto base = std::chrono::steady_clock::time_point(std::chrono::seconds(100));

  for (size_t i = 0; i < NeighborCache::kMaxEntries; i++) {
    IpAddress addr = IpAddress::V4(10, 0, static_cast<uint8>(i / 256),
                                   static_cast<uint8>(i % 256));
    cache.UpdateReachable(addr, kSampleMac1,
                          base + std::chrono::milliseconds(i));
  }
  EXPECT(NeighborCache::kMaxEntries, cache.Size());

  // Touch 10.0.0.0 so 10.0.0.1 becomes the oldest entry.
  EXPECT(true,
         cache
             .LookupMac(IpAddress::V4(10, 0, 0, 0),
                        base + std::chrono::seconds(5))
             .has_value());

  // Inserting a 257th entry evicts 10.0.0.1.
  IpAddress extra = IpAddress::V4(10, 0, 1, 0);
  cache.UpdateReachable(extra, kSampleMac2, base + std::chrono::seconds(6));
  EXPECT(NeighborCache::kMaxEntries, cache.Size());
  EXPECT(true, cache.Find(IpAddress::V4(10, 0, 0, 0)) != nullptr);
  EXPECT(true, cache.Find(IpAddress::V4(10, 0, 0, 1)) == nullptr);
  EXPECT(true, cache.Find(extra) != nullptr);
}

TEST(NeighborCache_ReachableTransitionsToStaleAfterExpiry) {
  NeighborCache cache;
  IpAddress peer = IpAddress::V4(10, 0, 2, 3);
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
  cache.UpdateReachable(peer, kSampleMac1, t0, std::chrono::seconds(5));

  EXPECT(true, cache.Find(peer)->state == NeighborState::Reachable);
  auto mac = cache.LookupMac(peer, t0 + std::chrono::seconds(6));
  ASSERT(true, mac.has_value());
  EXPECT(true, *mac == kSampleMac1);
  EXPECT(true, cache.Find(peer)->state == NeighborState::Stale);
}

TEST(EthernetAndIpv4_EthernetFramePadsTo60BytesAndParsesRoundTrip) {
  std::string payload = "hello";
  std::string frame =
      BuildEthernetFrame(kLocalMac, kSampleMac1, kEtherTypeIpv4, payload);
  EXPECT(kMinEthernetFrameSize, frame.size());

  auto parsed = ParseEthernetFrame(frame);
  ASSERT(true, parsed.has_value());
  EXPECT(true, parsed->src_mac == kLocalMac);
  EXPECT(true, parsed->dest_mac == kSampleMac1);
  EXPECT(kEtherTypeIpv4, parsed->ether_type);
  EXPECT(std::string_view(payload), parsed->payload.substr(0, payload.size()));
}

TEST(EthernetAndIpv4_MulticastMacMappingV4AndV6) {
  auto v4_mac = MulticastMac(IpAddress::V4(224, 0, 0, 251));
  ASSERT(true, v4_mac.has_value());
  std::array<uint8, 6> expected_v4 = {0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB};
  EXPECT(true, *v4_mac == expected_v4);

  auto v6_mac = MulticastMac(*IpAddress::Parse("ff02::1:ff12:3456"));
  ASSERT(true, v6_mac.has_value());
  std::array<uint8, 6> expected_v6 = {0x33, 0x33, 0xFF, 0x12, 0x34, 0x56};
  EXPECT(true, *v6_mac == expected_v6);

  EXPECT(false, MulticastMac(IpAddress::V4(10, 0, 2, 15)).has_value());
}

TEST(EthernetAndIpv4_ParseIpv4ClampsPaddingAndRejectsTruncatedTotalLength) {
  IpAddress src = IpAddress::V4(10, 0, 2, 2);
  IpAddress dst = IpAddress::V4(10, 0, 2, 15);
  std::string ipv4 = BuildIpv4Packet(src, dst, 17, "abcd");

  // Pad with trailing Ethernet zeros to 46 bytes; payload must stay 4 bytes.
  std::string padded = ipv4 + std::string(22, '\0');
  auto view = ParseIpv4Packet(padded);
  ASSERT(true, view.has_value());
  EXPECT(src, view->src);
  EXPECT(dst, view->dst);
  EXPECT(static_cast<uint8>(17), view->protocol);
  EXPECT(std::string_view("abcd"), view->payload);
  EXPECT(static_cast<size_t>(24), view->raw_ip_packet.size());

  // Truncate the buffer below total_length (regression test for A9 OOB read).
  std::string truncated = ipv4.substr(0, 22);
  EXPECT(false, ParseIpv4Packet(truncated).has_value());

  // Corrupt the IPv4 header checksum.
  std::string corrupted = ipv4;
  corrupted[10] ^= 0xFF;
  EXPECT(false, ParseIpv4Packet(corrupted).has_value());
}

}  // namespace
