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

#include "ndp.h"

#include "icmpv6.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Invalid hop limit (< 255) used to verify NDP hop-limit enforcement.
constexpr uint8 kInvalidNdpHopLimit = 254;

// Expected CurHopLimit in slirp's Router Advertisement.
constexpr uint8 kSlirpCurHopLimit = 64;

// Expected router lifetime in slirp's Router Advertisement (1800 seconds).
constexpr uint16 kSlirpRouterLifetime = 1800;

// Expected PIO valid lifetime in slirp's Router Advertisement (86400 seconds).
constexpr uint32 kSlirpValidLifetime = 86400;

// Expected PIO preferred lifetime in slirp's Router Advertisement (14400 s).
constexpr uint32 kSlirpPreferredLifetime = 14400;

// Expected RDNSS lifetime in slirp's Router Advertisement (3600 seconds).
constexpr uint32 kSlirpRdnssLifetime = 3600;

// Sample NAT64 PREF64 lifetime (multiple of 8 seconds).
constexpr uint32 kTestPref64Lifetime = 600;

Ipv6Header MakeNdpIpv6Header(const IpAddress& source,
                             const IpAddress& destination,
                             uint8 hop_limit = kNdpHopLimit) {
  Ipv6Header hdr;
  hdr.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  hdr.hop_limit = hop_limit;
  hdr.source = source;
  hdr.destination = destination;
  return hdr;
}

TEST(Ndp_ParseSlirpExactRouterAdvertisementBytes) {
  // Exact byte layout emitted by QEMU libslirp's router_input / ra_timer_handler
  // (src/ip6_icmp.c):
  //   ICMPv6 RA header (16 bytes):
  //     type=134, code=0, cksum, cur_hl=64, flags=0x00 (M=0,O=0),
  //     lifetime=1800 (0x0708), reachable=0, retrans=0
  //   SLLAO (8 bytes):
  //     type=1, len=1, mac=52:56:00:00:00:02
  //   PIO (32 bytes):
  //     type=3, len=4, prefix_len=64, flags=0xc0 (L=1,A=1),
  //     valid=86400 (0x00015180), preferred=14400 (0x00003840), reserved=0,
  //     prefix=fec0::
  //   RDNSS (24 bytes):
  //     type=25, len=3, reserved=0, lifetime=3600 (0x00000e10),
  //     addr=fec0::3
  constexpr std::array<uint8, 80> kSlirpRaBytes = {
      // RA header
      134, 0, 0x00, 0x00, 64, 0x00, 0x07, 0x08,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // Option 1: SLLAO (52:56:00:00:00:02)
      1, 1, 0x52, 0x56, 0x00, 0x00, 0x00, 0x02,
      // Option 3: PIO (fec0::/64, L=1, A=1, valid 86400, preferred 14400)
      3, 4, 64, 0xc0, 0x00, 0x01, 0x51, 0x80,
      0x00, 0x00, 0x38, 0x40, 0x00, 0x00, 0x00, 0x00,
      0xfe, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      // Option 25: RDNSS (fec0::3, lifetime 3600)
      25, 3, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x10,
      0xfe, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03,
  };

  IpAddress src = *IpAddress::Parse("fe80::2");
  IpAddress dst = AllNodesMulticastAddress();
  std::string packet(reinterpret_cast<const char*>(kSlirpRaBytes.data()),
                     kSlirpRaBytes.size());
  FinalizeIcmpv6Checksum(src, dst, packet);
  EXPECT(true, VerifyIcmpv6Checksum(src, dst, packet));

  Ipv6Header hdr = MakeNdpIpv6Header(src, dst);
  auto ra = ParseRouterAdvertisement(hdr, packet);
  ASSERT(true, ra.has_value());
  EXPECT(kSlirpCurHopLimit, ra->cur_hop_limit);
  EXPECT(false, ra->managed_flag);
  EXPECT(false, ra->other_config_flag);
  EXPECT(kSlirpRouterLifetime, ra->router_lifetime_seconds);
  EXPECT(static_cast<uint32>(0), ra->reachable_time_ms);
  EXPECT(static_cast<uint32>(0), ra->retrans_timer_ms);

  ASSERT(true, ra->source_mac.has_value());
  HardwareAddress expected_mac = {0x52, 0x56, 0x00, 0x00, 0x00, 0x02};
  EXPECT(expected_mac, *ra->source_mac);

  ASSERT(static_cast<size_t>(1), ra->prefixes.size());
  EXPECT(static_cast<uint8>(64), ra->prefixes[0].prefix_length);
  EXPECT(true, ra->prefixes[0].on_link);
  EXPECT(true, ra->prefixes[0].autonomous);
  EXPECT(kSlirpValidLifetime, ra->prefixes[0].valid_lifetime_seconds);
  EXPECT(kSlirpPreferredLifetime, ra->prefixes[0].preferred_lifetime_seconds);
  EXPECT(*IpAddress::Parse("fec0::"), ra->prefixes[0].prefix);

  ASSERT(static_cast<size_t>(1), ra->rdnss.size());
  EXPECT(kSlirpRdnssLifetime, ra->rdnss[0].lifetime_seconds);
  ASSERT(static_cast<size_t>(1), ra->rdnss[0].servers.size());
  EXPECT(*IpAddress::Parse("fec0::3"), ra->rdnss[0].servers[0]);
}

TEST(Ndp_RouterAdvertisementWithMtuDnsslAndPref64RoundTrip) {
  IpAddress src = *IpAddress::Parse("fe80::1");
  IpAddress dst = AllNodesMulticastAddress();

  NdpRouterAdvertisement ra;
  ra.cur_hop_limit = 64;
  ra.managed_flag = true;
  ra.other_config_flag = true;
  ra.router_lifetime_seconds = 900;
  ra.reachable_time_ms = 30000;
  ra.retrans_timer_ms = 1000;
  ra.source_mac = HardwareAddress{0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  ra.mtu = 1480;

  NdpDnsslOption dnssl;
  dnssl.lifetime_seconds = 1200;
  dnssl.domains = {"example.com", "corp.internal"};
  ra.dnssl.push_back(dnssl);

  NdpPref64Option pref64;
  pref64.prefix_length = 96;
  pref64.lifetime_seconds = kTestPref64Lifetime;
  pref64.prefix = *IpAddress::Parse("64:ff9b::");
  ra.pref64.push_back(pref64);

  std::string wire = BuildRouterAdvertisement(src, dst, ra);
  EXPECT(true, VerifyIcmpv6Checksum(src, dst, wire));

  auto parsed = ParseRouterAdvertisement(MakeNdpIpv6Header(src, dst), wire);
  ASSERT(true, parsed.has_value());
  EXPECT(true, parsed->managed_flag);
  EXPECT(true, parsed->other_config_flag);
  ASSERT(true, parsed->mtu.has_value());
  EXPECT(static_cast<uint32>(1480), *parsed->mtu);

  ASSERT(static_cast<size_t>(1), parsed->dnssl.size());
  EXPECT(static_cast<uint32>(1200), parsed->dnssl[0].lifetime_seconds);
  ASSERT(static_cast<size_t>(2), parsed->dnssl[0].domains.size());
  EXPECT(std::string("example.com"), parsed->dnssl[0].domains[0]);
  EXPECT(std::string("corp.internal"), parsed->dnssl[0].domains[1]);

  ASSERT(static_cast<size_t>(1), parsed->pref64.size());
  EXPECT(static_cast<uint8>(96), parsed->pref64[0].prefix_length);
  EXPECT(kTestPref64Lifetime, parsed->pref64[0].lifetime_seconds);
  EXPECT(*IpAddress::Parse("64:ff9b::"), parsed->pref64[0].prefix);
}

TEST(Ndp_ValidationRejectsBadHopLimitZeroLengthOptionAndNonLinkLocalRa) {
  IpAddress link_local = *IpAddress::Parse("fe80::2");
  IpAddress global_src = *IpAddress::Parse("fec0::2");
  IpAddress dst = AllNodesMulticastAddress();

  NdpRouterAdvertisement ra;
  ra.cur_hop_limit = 64;
  ra.router_lifetime_seconds = 600;
  std::string valid_ra = BuildRouterAdvertisement(link_local, dst, ra);

  // Hop limit 254 must be rejected.
  EXPECT(false,
         ParseRouterAdvertisement(
             MakeNdpIpv6Header(link_local, dst, kInvalidNdpHopLimit), valid_ra)
             .has_value());

  // Non-link-local source on RA must be rejected.
  EXPECT(false,
         ParseRouterAdvertisement(MakeNdpIpv6Header(global_src, dst), valid_ra)
             .has_value());

  // Appending an option with Length == 0 must cause the entire NDP message to
  // be rejected (RFC 4861 §6.1.2).
  std::string bad_opt_ra = valid_ra;
  bad_opt_ra.push_back(1);
  bad_opt_ra.push_back(0);
  bad_opt_ra.append(6, '\0');
  EXPECT(false,
         ParseRouterAdvertisement(MakeNdpIpv6Header(link_local, dst), bad_opt_ra)
             .has_value());
}

TEST(Ndp_NsNaRsRedirectCodecsAndValidation) {
  IpAddress host = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  IpAddress router = *IpAddress::Parse("fe80::2");
  HardwareAddress host_mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  HardwareAddress router_mac = {0x52, 0x56, 0x00, 0x00, 0x00, 0x02};

  // Router Solicitation round-trip.
  NdpRouterSolicitation rs;
  rs.source_mac = host_mac;
  IpAddress all_routers = AllRoutersMulticastAddress();
  std::string raw_rs = BuildRouterSolicitation(host, all_routers, rs);
  auto parsed_rs =
      ParseRouterSolicitation(MakeNdpIpv6Header(host, all_routers), raw_rs);
  ASSERT(true, parsed_rs.has_value());
  ASSERT(true, parsed_rs->source_mac.has_value());
  EXPECT(host_mac, *parsed_rs->source_mac);

  // Neighbor Solicitation round-trip and DAD validation.
  NdpNeighborSolicitation ns;
  ns.target = host;
  IpAddress sol_node = SolicitedNodeMulticastAddress(host);
  std::string raw_dad_ns =
      BuildNeighborSolicitation(IpAddress::V6Any(), sol_node, ns);
  auto parsed_dad_ns = ParseNeighborSolicitation(
      MakeNdpIpv6Header(IpAddress::V6Any(), sol_node), raw_dad_ns);
  ASSERT(true, parsed_dad_ns.has_value());
  EXPECT(host, parsed_dad_ns->target);
  // DAD NS addressed to unicast (not solicited-node multicast) is invalid.
  EXPECT(false,
         ParseNeighborSolicitation(MakeNdpIpv6Header(IpAddress::V6Any(), host),
                                   raw_dad_ns)
             .has_value());

  // Neighbor Advertisement round-trip.
  NdpNeighborAdvertisement na;
  na.router_flag = true;
  na.solicited_flag = true;
  na.override_flag = true;
  na.target = router;
  na.target_mac = router_mac;
  std::string raw_na = BuildNeighborAdvertisement(router, host, na);
  auto parsed_na =
      ParseNeighborAdvertisement(MakeNdpIpv6Header(router, host), raw_na);
  ASSERT(true, parsed_na.has_value());
  EXPECT(true, parsed_na->router_flag);
  EXPECT(true, parsed_na->solicited_flag);
  EXPECT(true, parsed_na->override_flag);
  EXPECT(router, parsed_na->target);
  ASSERT(true, parsed_na->target_mac.has_value());
  EXPECT(router_mac, *parsed_na->target_mac);

  // Multicast NA with Solicited=1 must be rejected (RFC 4861 §7.1.2).
  EXPECT(false,
         ParseNeighborAdvertisement(
             MakeNdpIpv6Header(router, AllNodesMulticastAddress()), raw_na)
             .has_value());

  // Redirect round-trip.
  NdpRedirect redir;
  redir.target = *IpAddress::Parse("fe80::3");
  redir.destination = *IpAddress::Parse("fec0::99");
  redir.target_mac = router_mac;
  std::string raw_redir = BuildRedirect(router, host, redir);
  auto parsed_redir =
      ParseRedirect(MakeNdpIpv6Header(router, host), raw_redir);
  ASSERT(true, parsed_redir.has_value());
  EXPECT(redir.target, parsed_redir->target);
  EXPECT(redir.destination, parsed_redir->destination);
}

TEST(Ndp_NudStateMachineTransitions) {
  IpAddress local = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  IpAddress peer = *IpAddress::Parse("fe80::2");
  HardwareAddress local_mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  HardwareAddress peer_mac = {0x52, 0x56, 0x00, 0x00, 0x00, 0x02};

  std::vector<Ipv6Datagram> sent;
  NudStateMachine nud(local, local_mac, [&](Ipv6Datagram dgram) {
    sent.push_back(std::move(dgram));
  });

  auto t = std::chrono::steady_clock::time_point{};
  // First touch creates Incomplete and sends multicast NS.
  EXPECT(false, nud.ResolveOrTouch(peer, t).has_value());
  ASSERT(static_cast<size_t>(1), sent.size());
  EXPECT(SolicitedNodeMulticastAddress(peer), sent.back().destination);
  ASSERT(true, nud.Find(peer) != nullptr);
  EXPECT(NudState::Incomplete, nud.Find(peer)->state);

  // Solicited NA moves Incomplete -> Reachable.
  NdpNeighborAdvertisement na;
  na.solicited_flag = true;
  na.override_flag = true;
  na.target = peer;
  na.target_mac = peer_mac;
  nud.OnNeighborAdvertisement(na, t);
  EXPECT(NudState::Reachable, nud.Find(peer)->state);
  EXPECT(peer_mac, *nud.ResolveOrTouch(peer, t));

  // After ReachableTime expires, OnTimer moves Reachable -> Stale.
  t += NudStateMachine::kDefaultReachableTime;
  nud.OnTimer(t);
  EXPECT(NudState::Stale, nud.Find(peer)->state);

  // Sending traffic in Stale transitions Stale -> Delay.
  EXPECT(peer_mac, *nud.ResolveOrTouch(peer, t));
  EXPECT(NudState::Delay, nud.Find(peer)->state);

  // After kDelayFirstProbeTime without confirmation, transitions Delay -> Probe
  // and emits a unicast NS.
  t += NudStateMachine::kDelayFirstProbeTime;
  nud.OnTimer(t);
  EXPECT(NudState::Probe, nud.Find(peer)->state);
  ASSERT(static_cast<size_t>(2), sent.size());
  EXPECT(peer, sent.back().destination);

  // Upper-layer confirmation returns Probe -> Reachable.
  nud.ConfirmReachability(peer, t);
  EXPECT(NudState::Reachable, nud.Find(peer)->state);
}

TEST(Ndp_RouterAdvertiserPeriodicAndSolicitedRas) {
  std::vector<Ipv6Datagram> sent;
  RouterAdvertiser advertiser([&](Ipv6Datagram dgram) {
    sent.push_back(std::move(dgram));
  });

  RouterAdvertisementConfig cfg;
  cfg.enabled = true;
  cfg.link_local_source = *IpAddress::Parse("fe80::1");
  cfg.source_mac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
  NdpPrefixInformation pio;
  pio.prefix_length = 64;
  pio.on_link = true;
  pio.autonomous = true;
  pio.valid_lifetime_seconds = 86400;
  pio.preferred_lifetime_seconds = 14400;
  pio.prefix = *IpAddress::Parse("fd00:1234::");
  cfg.prefixes.push_back(pio);

  auto t = std::chrono::steady_clock::time_point{};
  advertiser.SetConfig(cfg, t);
  ASSERT(static_cast<size_t>(1), sent.size());
  EXPECT(AllNodesMulticastAddress(), sent.back().destination);

  // Unicast RS gets an immediate unicast RA reply.
  IpAddress host = *IpAddress::Parse("fe80::99");
  NdpRouterSolicitation rs;
  rs.source_mac = HardwareAddress{0x02, 0x00, 0x00, 0x00, 0x00, 0x99};
  advertiser.OnRouterSolicitation(
      MakeNdpIpv6Header(host, AllRoutersMulticastAddress()), rs, t);
  ASSERT(static_cast<size_t>(2), sent.size());
  EXPECT(host, sent.back().destination);

  // Timer advances to next initial RA after 16 seconds.
  t += RouterAdvertiser::kMaxInitialRtrAdvertInterval;
  advertiser.OnTimer(t);
  ASSERT(static_cast<size_t>(3), sent.size());
  EXPECT(AllNodesMulticastAddress(), sent.back().destination);
}

}  // namespace
