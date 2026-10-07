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

#include "pmtu_cache.h"

#include <chrono>

#include "testing.h"

using ::perception::network::IpAddress;

namespace {

// Default Ethernet link MTU.
constexpr uint16 kEthernetMtu = 1500;

TEST(PmtuCache_Ipv6PacketTooBigAndMinimumMtuIgnore) {
  PmtuCache cache;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1000));
  IpAddress v6_dst = *IpAddress::Parse("2001:4860:4860::8888");

  // Default returns link MTU.
  EXPECT(kEthernetMtu, cache.GetPathMtu(v6_dst, kEthernetMtu, t0));

  // PTB with 1400 lowers the cached PMTU to 1400.
  auto updated = cache.OnPacketTooBig(v6_dst, 1400, kEthernetMtu, t0);
  ASSERT(true, updated.has_value());
  EXPECT(static_cast<uint16>(1400), *updated);
  EXPECT(static_cast<uint16>(1400), cache.GetPathMtu(v6_dst, kEthernetMtu, t0));

  // PTB higher than current PMTU (e.g. 1450) is ignored.
  EXPECT(false, cache.OnPacketTooBig(v6_dst, 1450, kEthernetMtu, t0).has_value());
  EXPECT(static_cast<uint16>(1400), cache.GetPathMtu(v6_dst, kEthernetMtu, t0));

  // PTB at the 1280 floor is accepted.
  auto at_floor = cache.OnPacketTooBig(v6_dst, 1280, kEthernetMtu, t0);
  ASSERT(true, at_floor.has_value());
  EXPECT(static_cast<uint16>(1280), *at_floor);

  // PTB below 1280 is ignored per RFC 8201 section 4.
  IpAddress v6_other = *IpAddress::Parse("2001:db8::99");
  EXPECT(false,
         cache.OnPacketTooBig(v6_other, 1279, kEthernetMtu, t0).has_value());
  EXPECT(false,
         cache.OnPacketTooBig(v6_other, 576, kEthernetMtu, t0).has_value());
  EXPECT(kEthernetMtu, cache.GetPathMtu(v6_other, kEthernetMtu, t0));
}

TEST(PmtuCache_Ipv4FloorClampAndRfc1191Plateaus) {
  PmtuCache cache;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1000));
  IpAddress v4_dst = IpAddress::V4(8, 8, 8, 8);

  // Reported MTU 0 steps down from 1500 to the next RFC 1191 plateau (1492),
  // then 1006, then 576.
  EXPECT(static_cast<uint16>(1492),
         *cache.OnPacketTooBig(v4_dst, 0, kEthernetMtu, t0));
  EXPECT(static_cast<uint16>(1006),
         *cache.OnPacketTooBig(v4_dst, 0, kEthernetMtu, t0));
  EXPECT(static_cast<uint16>(576),
         *cache.OnPacketTooBig(v4_dst, 0, kEthernetMtu, t0));

  // Positive value below 68 (e.g. 40) is clamped to the 68-byte IPv4 floor.
  EXPECT(static_cast<uint16>(68),
         *cache.OnPacketTooBig(v4_dst, 40, kEthernetMtu, t0));
  EXPECT(static_cast<uint16>(68), cache.GetPathMtu(v4_dst, kEthernetMtu, t0));
}

TEST(PmtuCache_TenMinuteExpiryAndPurge) {
  PmtuCache cache;
  auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(1000));
  IpAddress v6_dst = *IpAddress::Parse("2607:f8b0:4004:800::200e");

  cache.OnPacketTooBig(v6_dst, 1300, kEthernetMtu, t0);
  EXPECT(static_cast<uint16>(1300),
         cache.GetPathMtu(v6_dst, kEthernetMtu, t0 + std::chrono::seconds(599)));

  // At 10 minutes (600 s), the entry expires and reverts to link_mtu.
  EXPECT(kEthernetMtu,
         cache.GetPathMtu(v6_dst, kEthernetMtu, t0 + std::chrono::seconds(600)));

  EXPECT(static_cast<size_t>(1), cache.Size());
  cache.Purge(t0 + std::chrono::seconds(600));
  EXPECT(static_cast<size_t>(0), cache.Size());
}

}  // namespace
