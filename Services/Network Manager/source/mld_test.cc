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

#include "mld.h"

#include "icmpv6.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Deterministic delay in milliseconds returned by the test delay generator.
constexpr uint32 kFixedRandomDelayMs = 250;

// Sample Maximum Response Code below 32768 (10000 ms = 10 s).
constexpr uint16 kLinearMaxRespCode = 10000;

// Sample floating-point Maximum Response Code: exp=1, mant=0 -> 0x9000 ->
// (0x1000 << 4) = 65536 ms.
constexpr uint16 kFloatMaxRespCode = 0x9000;

// Expected milliseconds for kFloatMaxRespCode.
constexpr int64 kFloatMaxRespExpectedMs = 65536;

std::string BuildTestMldQuery(const IpAddress& source,
                              const IpAddress& destination,
                              uint16 max_resp_code,
                              const IpAddress& multicast_address,
                              bool v2) {
  std::string out;
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(Icmpv6Type::MulticastListenerQuery));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU16(max_resp_code);
  writer.WriteU16(0);
  writer.WriteIpv6Address(multicast_address);
  if (v2) {
    writer.WriteU8(2);
    writer.WriteU8(125);
    writer.WriteU16(0);
  }
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

TEST(Mld_ReportBuildHopByHopRouterAlertAndHopLimitOne) {
  IpAddress ll = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  IpAddress group = SolicitedNodeMulticastAddress(ll);

  std::vector<Ipv6Datagram> sent;
  MldManager mld(
      [&](Ipv6Datagram dgram) { sent.push_back(std::move(dgram)); },
      [](uint32) { return kFixedRandomDelayMs; });
  mld.SetLinkLocalSource(ll);

  auto t = std::chrono::steady_clock::time_point{};
  // All-nodes ff02::1 is never reported.
  EXPECT(false, IsMldReportableGroup(AllNodesMulticastAddress()));
  EXPECT(true, mld.JoinGroup(AllNodesMulticastAddress(), t));
  EXPECT(static_cast<size_t>(0), sent.size());

  // Joining a solicited-node group emits an immediate MLDv2 Report to ff02::16
  // with hop_limit = 1 and router_alert = true.
  EXPECT(true, mld.JoinGroup(group, t));
  ASSERT(static_cast<size_t>(1), sent.size());
  EXPECT(ll, sent.back().source);
  EXPECT(AllMldv2RoutersMulticastAddress(), sent.back().destination);
  EXPECT(kMldHopLimit, sent.back().hop_limit);
  EXPECT(true, sent.back().router_alert);

  // Verify the serialized IPv6 packet contains the Hop-by-Hop Router Alert
  // option and a valid MLDv2 ChangeToExcludeMode record.
  std::string wire_pkt = SerializeIpv6Datagram(sent.back());
  auto ip_hdr = ParseIpv6Header(wire_pkt);
  ASSERT(true, ip_hdr.has_value());
  EXPECT(kMldHopLimit, ip_hdr->hop_limit);
  ExtensionWalkResult walk = WalkExtensionHeaders(*ip_hdr, wire_pkt);
  EXPECT(ExtensionWalkStatus::UpperLayer, walk.status);
  EXPECT(true, walk.router_alert);
  EXPECT(static_cast<uint16>(0), walk.router_alert_value);

  auto records = ParseMldv2Report(walk.payload);
  ASSERT(true, records.has_value());
  ASSERT(static_cast<size_t>(1), records->size());
  EXPECT(MldRecordType::ChangeToExcludeMode, (*records)[0].record_type);
  EXPECT(group, (*records)[0].multicast_address);

  // Second state-change transmission fires after the random delay.
  t += std::chrono::milliseconds(kFixedRandomDelayMs);
  mld.OnTimer(t);
  ASSERT(static_cast<size_t>(2), sent.size());

  // Leaving the group emits ChangeToIncludeMode.
  EXPECT(true, mld.LeaveGroup(group, t));
  ASSERT(static_cast<size_t>(3), sent.size());
  auto leave_records = ParseMldv2Report(sent.back().payload);
  ASSERT(true, leave_records.has_value());
  ASSERT(static_cast<size_t>(1), leave_records->size());
  EXPECT(MldRecordType::ChangeToIncludeMode, (*leave_records)[0].record_type);
}

TEST(Mld_QueryParsingAndResponseTiming) {
  EXPECT(std::chrono::milliseconds(kLinearMaxRespCode),
         DecodeMldv2MaxResponseCode(kLinearMaxRespCode));
  EXPECT(std::chrono::milliseconds(kFloatMaxRespExpectedMs),
         DecodeMldv2MaxResponseCode(kFloatMaxRespCode));

  IpAddress router = *IpAddress::Parse("fe80::2");
  IpAddress all_nodes = AllNodesMulticastAddress();
  std::string raw_query = BuildTestMldQuery(router, all_nodes, 1000,
                                            IpAddress::V6Any(), true);

  Ipv6Header hdr;
  hdr.source = router;
  hdr.destination = all_nodes;
  hdr.hop_limit = kMldHopLimit;
  hdr.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);

  // Query without Router Alert or with hop limit != 1 is rejected.
  EXPECT(false, ParseMldQuery(hdr, false, raw_query).has_value());
  Ipv6Header bad_hl = hdr;
  bad_hl.hop_limit = 255;
  EXPECT(false, ParseMldQuery(bad_hl, true, raw_query).has_value());

  auto parsed = ParseMldQuery(hdr, true, raw_query);
  ASSERT(true, parsed.has_value());
  EXPECT(false, parsed->is_v1);
  EXPECT(std::chrono::milliseconds(1000), parsed->max_response_delay);

  // General query schedules a ModeIsExclude current-state report after the
  // chosen delay.
  IpAddress ll = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  IpAddress group = SolicitedNodeMulticastAddress(ll);
  std::vector<Ipv6Datagram> sent;
  MldManager mld(
      [&](Ipv6Datagram dgram) { sent.push_back(std::move(dgram)); },
      [](uint32) { return kFixedRandomDelayMs; });
  mld.SetLinkLocalSource(ll);

  auto t = std::chrono::steady_clock::time_point{};
  mld.JoinGroup(group, t);
  t += std::chrono::seconds(2);
  mld.OnTimer(t);
  sent.clear();

  mld.OnQuery(*parsed, t);
  EXPECT(static_cast<size_t>(0), sent.size());
  mld.OnTimer(t + std::chrono::milliseconds(kFixedRandomDelayMs - 1));
  EXPECT(static_cast<size_t>(0), sent.size());
  mld.OnTimer(t + std::chrono::milliseconds(kFixedRandomDelayMs));
  ASSERT(static_cast<size_t>(1), sent.size());
  auto records = ParseMldv2Report(sent.back().payload);
  ASSERT(true, records.has_value());
  ASSERT(static_cast<size_t>(1), records->size());
  EXPECT(MldRecordType::ModeIsExclude, (*records)[0].record_type);
}

TEST(Mld_V1CompatibilityMode) {
  IpAddress router = *IpAddress::Parse("fe80::2");
  IpAddress ll = *IpAddress::Parse("fe80::5054:ff:fe12:3456");
  IpAddress group = SolicitedNodeMulticastAddress(ll);

  std::string v1_query_bytes = BuildTestMldQuery(
      router, AllNodesMulticastAddress(), 500, IpAddress::V6Any(), false);
  Ipv6Header hdr;
  hdr.source = router;
  hdr.destination = AllNodesMulticastAddress();
  hdr.hop_limit = kMldHopLimit;

  auto v1_query = ParseMldQuery(hdr, true, v1_query_bytes);
  ASSERT(true, v1_query.has_value());
  EXPECT(true, v1_query->is_v1);

  std::vector<Ipv6Datagram> sent;
  MldManager mld(
      [&](Ipv6Datagram dgram) { sent.push_back(std::move(dgram)); },
      [](uint32) { return kFixedRandomDelayMs; });
  mld.SetLinkLocalSource(ll);

  auto t = std::chrono::steady_clock::time_point{};
  mld.OnQuery(*v1_query, t);
  EXPECT(true, mld.IsMldv1Mode(t));

  // Joining a group in MLDv1 compatibility mode sends an MLDv1 Report (131)
  // addressed to the group itself.
  mld.JoinGroup(group, t);
  ASSERT(static_cast<size_t>(1), sent.size());
  EXPECT(group, sent.back().destination);
  EXPECT(static_cast<uint8>(Icmpv6Type::MulticastListenerReportV1),
         static_cast<uint8>(sent.back().payload[0]));

  // Leaving sends an MLDv1 Done (132) to ff02::2.
  mld.LeaveGroup(group, t);
  ASSERT(static_cast<size_t>(2), sent.size());
  EXPECT(AllRoutersMulticastAddress(), sent.back().destination);
  EXPECT(static_cast<uint8>(Icmpv6Type::MulticastListenerDone),
         static_cast<uint8>(sent.back().payload[0]));
}

}  // namespace
