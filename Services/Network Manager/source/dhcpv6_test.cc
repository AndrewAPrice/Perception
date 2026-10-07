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

#include "dhcpv6.h"

#include "testing.h"

namespace {

using ::perception::network::IpAddress;

// Sample QEMU MAC address (52:54:00:12:34:56).
constexpr HardwareAddress kTestMac = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

// Sample server MAC address for constructing a server DUID-LL.
constexpr HardwareAddress kServerMac = {0x52, 0x55, 0xfe, 0xc0, 0x00, 0x02};

TEST(Dhcpv6DuidLinkLayerEncoding) {
  std::string duid = BuildDuidLinkLayer(kTestMac);
  ASSERT(10u, duid.size());
  EXPECT(0x00u, static_cast<uint8>(duid[0]));
  EXPECT(0x03u, static_cast<uint8>(duid[1]));
  EXPECT(0x00u, static_cast<uint8>(duid[2]));
  EXPECT(0x01u, static_cast<uint8>(duid[3]));
  EXPECT(0x52u, static_cast<uint8>(duid[4]));
  EXPECT(0x56u, static_cast<uint8>(duid[9]));
}

TEST(Dhcpv6StatelessInformationRequestAndReply) {
  std::vector<std::string> sent;
  Dhcpv6Client client(
      kTestMac, [&](std::string payload) { sent.push_back(std::move(payload)); },
      []() { return 0xabcdefu; });

  const auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(10));
  client.OnRouterAdvertisementFlags(false, true, t0);
  EXPECT(Dhcpv6State::StatelessRequesting, client.state());
  ASSERT(1u, sent.size());

  auto parsed_req = ParseDhcpv6Message(sent[0]);
  ASSERT(true, parsed_req.has_value());
  EXPECT(Dhcpv6MessageType::InformationRequest, parsed_req->type);
  EXPECT(0xabcdefu, parsed_req->transaction_id);
  EXPECT(client.client_duid(), parsed_req->client_id);
  ASSERT(true, parsed_req->elapsed_time_centiseconds.has_value());
  EXPECT(0u, *parsed_req->elapsed_time_centiseconds);

  // Exponential backoff retransmission after 1 second.
  client.OnTimer(t0 + std::chrono::seconds(1));
  ASSERT(2u, sent.size());
  auto parsed_retry = ParseDhcpv6Message(sent[1]);
  ASSERT(true, parsed_retry.has_value());
  EXPECT(100u, *parsed_retry->elapsed_time_centiseconds);

  // Server replies with slirp's DNS server (fec0::3) and a 1200s refresh time.
  const IpAddress slirp_dns = *IpAddress::Parse("fec0::3");
  Dhcpv6Message reply;
  reply.type = Dhcpv6MessageType::Reply;
  reply.transaction_id = 0xabcdefu;
  reply.client_id = client.client_duid();
  reply.server_id = BuildDuidLinkLayer(kServerMac);
  reply.dns_servers.push_back(slirp_dns);
  reply.info_refresh_time_seconds = 1200;

  client.OnPacket(BuildDhcpv6Message(reply), t0 + std::chrono::seconds(1));
  EXPECT(Dhcpv6State::StatelessBound, client.state());
  ASSERT(1u, client.dns_servers().size());
  EXPECT(slirp_dns, client.dns_servers()[0]);

  // When the refresh deadline fires, the client sends a new Information-Request.
  client.OnTimer(t0 + std::chrono::seconds(1201));
  EXPECT(Dhcpv6State::StatelessRequesting, client.state());
  EXPECT(3u, sent.size());
}

TEST(Dhcpv6StatefulFourWayExchangeAndRenewal) {
  std::vector<std::string> sent;
  uint32 next_tx_id = 0x111111u;
  Dhcpv6Client client(
      kTestMac, [&](std::string payload) { sent.push_back(std::move(payload)); },
      [&]() { return next_tx_id++; });

  const auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(100));
  client.OnRouterAdvertisementFlags(true, false, t0);
  EXPECT(Dhcpv6State::Soliciting, client.state());
  ASSERT(1u, sent.size());

  auto solicit = ParseDhcpv6Message(sent[0]);
  ASSERT(true, solicit.has_value());
  EXPECT(Dhcpv6MessageType::Solicit, solicit->type);
  ASSERT(1u, solicit->ia_nas.size());
  EXPECT(client.iaid(), solicit->ia_nas[0].iaid);

  // Server sends an Advertise offering 2001:db8::42.
  const IpAddress leased_ip = *IpAddress::Parse("2001:db8::42");
  Dhcpv6Message advertise;
  advertise.type = Dhcpv6MessageType::Advertise;
  advertise.transaction_id = solicit->transaction_id;
  advertise.client_id = client.client_duid();
  advertise.server_id = BuildDuidLinkLayer(kServerMac);
  Dhcpv6IaNa ia;
  ia.iaid = client.iaid();
  ia.t1_seconds = 300;
  ia.t2_seconds = 480;
  ia.addresses.push_back({leased_ip, 600, 1200});
  advertise.ia_nas.push_back(ia);

  client.OnPacket(BuildDhcpv6Message(advertise), t0 + std::chrono::seconds(1));
  EXPECT(Dhcpv6State::Requesting, client.state());
  ASSERT(2u, sent.size());

  auto request = ParseDhcpv6Message(sent[1]);
  ASSERT(true, request.has_value());
  EXPECT(Dhcpv6MessageType::Request, request->type);
  EXPECT(advertise.server_id, request->server_id);
  ASSERT(1u, request->ia_nas.size());
  ASSERT(1u, request->ia_nas[0].addresses.size());
  EXPECT(leased_ip, request->ia_nas[0].addresses[0].address);

  // Server confirms the lease with a Reply.
  Dhcpv6Message reply = advertise;
  reply.type = Dhcpv6MessageType::Reply;
  reply.transaction_id = request->transaction_id;
  client.OnPacket(BuildDhcpv6Message(reply), t0 + std::chrono::seconds(2));

  EXPECT(Dhcpv6State::Bound, client.state());
  ASSERT(1u, client.addresses().size());
  EXPECT(leased_ip, client.addresses()[0].address);
  EXPECT(128u, client.addresses()[0].prefix_length);
  EXPECT(AddressOrigin::Dhcpv6, client.addresses()[0].origin);

  // Advancing to T1 (t0 + 2s + 300s) triggers Renew.
  client.OnTimer(t0 + std::chrono::seconds(302));
  EXPECT(Dhcpv6State::Renewing, client.state());
  ASSERT(3u, sent.size());
  auto renew = ParseDhcpv6Message(sent[2]);
  ASSERT(true, renew.has_value());
  EXPECT(Dhcpv6MessageType::Renew, renew->type);

  // Advancing to T2 (t0 + 2s + 480s) without a Reply transitions to Rebinding.
  client.OnTimer(t0 + std::chrono::seconds(482));
  EXPECT(Dhcpv6State::Rebinding, client.state());
  ASSERT(4u, sent.size());
  auto rebind = ParseDhcpv6Message(sent[3]);
  ASSERT(true, rebind.has_value());
  EXPECT(Dhcpv6MessageType::Rebind, rebind->type);

  // Releasing sends a Release message and clears the lease.
  client.Release(t0 + std::chrono::seconds(490));
  EXPECT(Dhcpv6State::Idle, client.state());
  EXPECT(0u, client.addresses().size());
  ASSERT(5u, sent.size());
  auto release = ParseDhcpv6Message(sent[4]);
  ASSERT(true, release.has_value());
  EXPECT(Dhcpv6MessageType::Release, release->type);
}

}  // namespace
