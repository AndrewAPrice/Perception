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

#include "slaac.h"

#include "testing.h"

using ::perception::network::IpAddress;

namespace {

// Sample valid lifetime larger than 2 hours (86400 seconds = 24 hours).
constexpr uint32 kOneDaySeconds = 86400;

// Sample preferred lifetime (14400 seconds = 4 hours).
constexpr uint32 kFourHoursSeconds = 14400;

// Sample valid lifetime shorter than 2 hours (600 seconds = 10 minutes).
constexpr uint32 kTenMinutesSeconds = 600;

// Sample remaining lifetime below 2 hours (3600 seconds = 1 hour).
constexpr uint32 kOneHourSeconds = 3600;

TEST(Slaac_ModifiedEui64AndLinkLocalAddress) {
  HardwareAddress mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  std::array<uint8, 8> expected_iid = {0x50, 0x54, 0x00, 0xff,
                                       0xfe, 0x12, 0x34, 0x56};
  EXPECT(expected_iid, ModifiedEui64InterfaceIdentifier(mac));
  EXPECT(*IpAddress::Parse("fe80::5054:ff:fe12:3456"),
         LinkLocalAddressFromMac(mac));
}

TEST(Slaac_LinkLocalDadAndRouterSolicitationAndGlobalPio) {
  HardwareAddress mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  std::vector<Ipv6Datagram> sent;
  SlaacController slaac(mac, [&](Ipv6Datagram dgram) {
    sent.push_back(std::move(dgram));
  });

  auto t = std::chrono::steady_clock::time_point{};
  slaac.BringUpLinkLocal(t);

  IpAddress ll = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  const InterfaceAddress* ll_entry = slaac.FindAddress(ll);
  ASSERT(true, ll_entry != nullptr);
  EXPECT(AddressState::Tentative, ll_entry->state);
  EXPECT(false, slaac.UsableLinkLocalAddress().has_value());

  // Bring-up emits 1 DAD NS with unspecified source to solicited-node group.
  ASSERT(static_cast<size_t>(1), sent.size());
  EXPECT(IpAddress::V6Any(), sent.back().source);
  EXPECT(SolicitedNodeMulticastAddress(ll), sent.back().destination);

  // After 1 second with no conflict, link-local becomes Preferred and an RS is
  // sent to ff02::2.
  t += SlaacController::kDefaultRetransTimer;
  slaac.OnTimer(t);
  EXPECT(AddressState::Preferred, slaac.FindAddress(ll)->state);
  EXPECT(ll, *slaac.UsableLinkLocalAddress());
  ASSERT(static_cast<size_t>(2), sent.size());
  EXPECT(ll, sent.back().source);
  EXPECT(AllRoutersMulticastAddress(), sent.back().destination);

  // Router Advertisement with fec0::/64 A=1 creates tentative global address.
  NdpRouterAdvertisement ra;
  NdpPrefixInformation pio;
  pio.prefix_length = 64;
  pio.on_link = true;
  pio.autonomous = true;
  pio.valid_lifetime_seconds = kOneDaySeconds;
  pio.preferred_lifetime_seconds = kFourHoursSeconds;
  pio.prefix = *IpAddress::Parse("fec0::");
  ra.prefixes.push_back(pio);
  slaac.OnRouterAdvertisement(ra, t);

  IpAddress global = *IpAddress::Parse("fec0::5054:ff:fe12:3456");
  ASSERT(true, slaac.FindAddress(global) != nullptr);
  EXPECT(AddressState::Tentative, slaac.FindAddress(global)->state);

  // Complete DAD for the global address.
  t += SlaacController::kDefaultRetransTimer;
  slaac.OnTimer(t);
  EXPECT(AddressState::Preferred, slaac.FindAddress(global)->state);

  // Incoming NS for the preferred global address triggers a solicited NA.
  Ipv6Header ns_hdr;
  ns_hdr.source = *IpAddress::Parse("fe80::2");
  ns_hdr.destination = SolicitedNodeMulticastAddress(global);
  ns_hdr.hop_limit = kNdpHopLimit;
  NdpNeighborSolicitation ns;
  ns.target = global;
  size_t before = sent.size();
  slaac.OnNeighborSolicitation(ns_hdr, ns, t);
  ASSERT(before + 1, sent.size());
  EXPECT(global, sent.back().source);
  EXPECT(ns_hdr.source, sent.back().destination);
}

TEST(Slaac_DadFailureOnAdvertisementOrCompetingDadSolicitation) {
  HardwareAddress mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  SlaacController slaac(mac, {});
  auto t = std::chrono::steady_clock::time_point{};
  slaac.BringUpLinkLocal(t);

  IpAddress ll = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  NdpNeighborAdvertisement na;
  na.target = ll;
  slaac.OnNeighborAdvertisement(na, t);
  EXPECT(AddressState::Duplicate, slaac.FindAddress(ll)->state);

  // Competing DAD NS (source == ::) for a tentative address also marks it
  // Duplicate.
  InterfaceAddress extra;
  extra.address = *IpAddress::Parse("fec0::99");
  extra.prefix_length = 64;
  extra.origin = AddressOrigin::Slaac;
  slaac.AddTentativeAddress(extra, t);
  EXPECT(AddressState::Tentative, slaac.FindAddress(extra.address)->state);

  Ipv6Header dad_hdr;
  dad_hdr.source = IpAddress::V6Any();
  dad_hdr.destination = SolicitedNodeMulticastAddress(extra.address);
  dad_hdr.hop_limit = kNdpHopLimit;
  NdpNeighborSolicitation dad_ns;
  dad_ns.target = extra.address;
  slaac.OnNeighborSolicitation(dad_hdr, dad_ns, t);
  EXPECT(AddressState::Duplicate, slaac.FindAddress(extra.address)->state);
}

TEST(Slaac_TwoHourRuleAndLifetimeDeprecationAndExpiry) {
  auto t0 = std::chrono::steady_clock::time_point{};
  auto existing_24h = t0 + std::chrono::seconds(kOneDaySeconds);

  // Advertised 10 minutes (< 2h) when remaining is 24h (> 2h) clamps valid_until
  // to t0 + 2 hours (RFC 4862 §5.5.3(e)).
  EXPECT(t0 + std::chrono::seconds(kSlaacTwoHoursSeconds),
         ComputeUpdatedValidUntil(existing_24h, kTenMinutesSeconds, t0));

  // Advertised 10 minutes when remaining is 1 hour (<= 2h) leaves valid_until
  // unchanged.
  auto existing_1h = t0 + std::chrono::seconds(kOneHourSeconds);
  EXPECT(existing_1h,
         ComputeUpdatedValidUntil(existing_1h, kTenMinutesSeconds, t0));

  // Advertised 24 hours (> 2h and > remaining) extends valid_until to 24h.
  EXPECT(existing_24h,
         ComputeUpdatedValidUntil(existing_1h, kOneDaySeconds, t0));

  // Verify Preferred -> Deprecated -> Removed lifecycle in SlaacController.
  HardwareAddress mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  SlaacController slaac(mac, {});
  NdpPrefixInformation pio;
  pio.prefix_length = 64;
  pio.autonomous = true;
  pio.preferred_lifetime_seconds = 100;
  pio.valid_lifetime_seconds = 200;
  pio.prefix = *IpAddress::Parse("fec0::");
  slaac.OnPrefixInformation(pio, t0);

  IpAddress global = *IpAddress::Parse("fec0::5054:ff:fe12:3456");
  slaac.OnTimer(t0 + SlaacController::kDefaultRetransTimer);
  EXPECT(AddressState::Preferred, slaac.FindAddress(global)->state);

  slaac.OnTimer(t0 + std::chrono::seconds(100));
  EXPECT(AddressState::Deprecated, slaac.FindAddress(global)->state);

  slaac.OnTimer(t0 + std::chrono::seconds(200));
  EXPECT(true, slaac.FindAddress(global) == nullptr);
}

}  // namespace
