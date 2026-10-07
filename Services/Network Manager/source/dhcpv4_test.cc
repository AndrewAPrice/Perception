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

#include "dhcpv4.h"

#include "testing.h"

namespace {

using ::perception::network::IpAddress;

// Sample QEMU MAC address (52:54:00:12:34:56).
constexpr HardwareAddress kTestMac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

// Conflicting peer MAC address on the link.
constexpr HardwareAddress kOtherMac = {0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc};

TEST(Dhcpv4SubnetMaskAndLinkLocalSelection) {
  EXPECT(24u, SubnetMaskToPrefixLength(IpAddress::V4(255, 255, 255, 0)));
  EXPECT(16u, SubnetMaskToPrefixLength(IpAddress::V4(255, 255, 0, 0)));
  EXPECT(8u, SubnetMaskToPrefixLength(IpAddress::V4(255, 0, 0, 0)));

  // RFC 3927 §2.1 excludes 169.254.0.0/24 and 169.254.255.0/24.
  EXPECT(IpAddress::V4(169, 254, 1, 0), SelectIpv4LinkLocalCandidate(0));
  EXPECT(IpAddress::V4(169, 254, 254, 255),
         SelectIpv4LinkLocalCandidate(65023));
  EXPECT(IpAddress::V4(169, 254, 1, 0), SelectIpv4LinkLocalCandidate(65024));
}

TEST(Dhcpv4DiscoverOfferRequestAckAndRenew) {
  struct SentPacket {
    std::string payload;
    IpAddress src_ip;
    IpAddress dst_ip;
  };
  std::vector<SentPacket> sent;
  uint32 next_rng = 0x12345678u;

  Dhcpv4Client client(
      kTestMac,
      [&](std::string payload, const IpAddress& src_ip,
          const IpAddress& dst_ip) {
        sent.push_back({std::move(payload), src_ip, dst_ip});
      },
      {}, [&]() { return next_rng++; });

  const auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
  client.Start(t0);
  EXPECT(Dhcpv4State::Selecting, client.state());
  ASSERT(1u, sent.size());
  EXPECT(IpAddress::V4Any(), sent[0].src_ip);
  EXPECT(IpAddress::V4Broadcast(), sent[0].dst_ip);

  auto discover = ParseDhcpv4Message(sent[0].payload);
  ASSERT(true, discover.has_value());
  EXPECT(Dhcpv4MessageType::Discover, discover->type);
  EXPECT(0x12345678u, discover->transaction_id);

  // Server responds with a DHCPOFFER for 10.0.2.15/24, router 10.0.2.2, DNS 10.0.2.3.
  Dhcpv4Message offer;
  offer.op = 2;
  offer.transaction_id = discover->transaction_id;
  offer.your_ip = IpAddress::V4(10, 0, 2, 15);
  offer.client_mac = kTestMac;
  offer.type = Dhcpv4MessageType::Offer;
  offer.server_id = IpAddress::V4(10, 0, 2, 2);
  offer.subnet_mask = IpAddress::V4(255, 255, 255, 0);
  offer.routers = {IpAddress::V4(10, 0, 2, 2)};
  offer.dns_servers = {IpAddress::V4(10, 0, 2, 3)};
  offer.lease_time_seconds = 600;

  client.OnPacket(BuildDhcpv4Message(offer), t0 + std::chrono::seconds(1));
  EXPECT(Dhcpv4State::Requesting, client.state());
  ASSERT(2u, sent.size());

  auto request = ParseDhcpv4Message(sent[1].payload);
  ASSERT(true, request.has_value());
  EXPECT(Dhcpv4MessageType::Request, request->type);
  ASSERT(true, request->requested_ip.has_value());
  EXPECT(IpAddress::V4(10, 0, 2, 15), *request->requested_ip);
  ASSERT(true, request->server_id.has_value());
  EXPECT(IpAddress::V4(10, 0, 2, 2), *request->server_id);

  // Server confirms with DHCPACK.
  Dhcpv4Message ack = offer;
  ack.type = Dhcpv4MessageType::Ack;
  client.OnPacket(BuildDhcpv4Message(ack), t0 + std::chrono::seconds(2));

  EXPECT(Dhcpv4State::Bound, client.state());
  ASSERT(true, client.address().has_value());
  EXPECT(IpAddress::V4(10, 0, 2, 15), client.address()->address);
  EXPECT(24u, client.address()->prefix_length);
  EXPECT(AddressOrigin::Dhcpv4, client.address()->origin);
  ASSERT(true, client.default_router().has_value());
  EXPECT(IpAddress::V4(10, 0, 2, 2), *client.default_router());
  ASSERT(1u, client.dns_servers().size());
  EXPECT(IpAddress::V4(10, 0, 2, 3), client.dns_servers()[0]);

  // Advancing to T1 (300s after ACK) transitions to Renewing and unicasts Request.
  client.OnTimer(t0 + std::chrono::seconds(302));
  EXPECT(Dhcpv4State::Renewing, client.state());
  ASSERT(3u, sent.size());
  EXPECT(IpAddress::V4(10, 0, 2, 15), sent[2].src_ip);
  EXPECT(IpAddress::V4(10, 0, 2, 2), sent[2].dst_ip);

  // Release clears the lease and sends a DHCPRELEASE.
  client.Release(t0 + std::chrono::seconds(305));
  EXPECT(Dhcpv4State::Idle, client.state());
  EXPECT(false, client.address().has_value());
  ASSERT(4u, sent.size());
  auto release = ParseDhcpv4Message(sent[3].payload);
  ASSERT(true, release.has_value());
  EXPECT(Dhcpv4MessageType::Release, release->type);
}

TEST(Dhcpv4LinkLocalFallbackArpProbingAndBackgroundRecovery) {
  std::vector<std::string> dhcp_sent;
  std::vector<std::pair<IpAddress, IpAddress>> arp_probes;
  uint32 next_rng = 10;

  Dhcpv4Client client(
      kTestMac,
      [&](std::string payload, const IpAddress&, const IpAddress&) {
        dhcp_sent.push_back(std::move(payload));
      },
      [&](const IpAddress& sender_ip, const IpAddress& target_ip) {
        arp_probes.push_back({sender_ip, target_ip});
      },
      [&]() { return next_rng++; });

  auto now = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  client.Start(now);
  EXPECT(1u, dhcp_sent.size());

  // Second Discover at +4s, third Discover at +4s + 8s = +12s.
  now += std::chrono::seconds(4);
  client.OnTimer(now);
  EXPECT(2u, dhcp_sent.size());

  now += std::chrono::seconds(8);
  client.OnTimer(now);
  EXPECT(3u, dhcp_sent.size());

  // Timeout after the third Discover (+16s) enters LinkLocalProbing and sends
  // the first RFC 3927 ARP probe (0.0.0.0 -> 169.254.1.11).
  now += std::chrono::seconds(16);
  client.OnTimer(now);
  EXPECT(Dhcpv4State::LinkLocalProbing, client.state());
  ASSERT(1u, arp_probes.size());
  const IpAddress first_candidate = arp_probes[0].second;
  EXPECT(IpAddress::V4Any(), arp_probes[0].first);
  EXPECT(true, first_candidate.IsLinkLocal());

  // Simulate an ARP conflict on `first_candidate`: client immediately picks a
  // new candidate and restarts probing.
  now += std::chrono::milliseconds(500);
  client.OnArpPacket(first_candidate, first_candidate, kOtherMac, now);
  ASSERT(2u, arp_probes.size());
  const IpAddress second_candidate = arp_probes[1].second;
  EXPECT(false, first_candidate == second_candidate);

  // Advance through the remaining 2 probes and the announcement step (3 x 1s).
  for (int i = 0; i < 3; ++i) {
    now += std::chrono::seconds(1);
    client.OnTimer(now);
  }
  EXPECT(Dhcpv4State::LinkLocalBound, client.state());
  ASSERT(true, client.address().has_value());
  EXPECT(second_candidate, client.address()->address);
  EXPECT(16u, client.address()->prefix_length);
  EXPECT(AddressOrigin::LinkLocal, client.address()->origin);

  // While LinkLocalBound, the background DHCPv4 retry fires and a server later
  // responds with an Offer + Ack, upgrading the interface to a DHCPv4 lease.
  now += std::chrono::seconds(30);
  client.OnTimer(now);
  ASSERT(4u, dhcp_sent.size());
  auto bg_discover = ParseDhcpv4Message(dhcp_sent.back());
  ASSERT(true, bg_discover.has_value());

  Dhcpv4Message offer;
  offer.op = 2;
  offer.transaction_id = bg_discover->transaction_id;
  offer.your_ip = IpAddress::V4(192, 168, 1, 50);
  offer.client_mac = kTestMac;
  offer.type = Dhcpv4MessageType::Offer;
  offer.server_id = IpAddress::V4(192, 168, 1, 1);
  offer.subnet_mask = IpAddress::V4(255, 255, 255, 0);
  offer.lease_time_seconds = 3600;
  client.OnPacket(BuildDhcpv4Message(offer), now);
  EXPECT(Dhcpv4State::Requesting, client.state());

  Dhcpv4Message ack = offer;
  ack.type = Dhcpv4MessageType::Ack;
  client.OnPacket(BuildDhcpv4Message(ack), now);
  EXPECT(Dhcpv4State::Bound, client.state());
  ASSERT(true, client.address().has_value());
  EXPECT(IpAddress::V4(192, 168, 1, 50), client.address()->address);
  EXPECT(AddressOrigin::Dhcpv4, client.address()->origin);
}

}  // namespace
