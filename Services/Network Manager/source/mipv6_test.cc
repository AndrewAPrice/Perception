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

#include "mipv6.h"

#include <chrono>
#include <string>

#include "ipv6_header.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// UDP protocol number for test payloads.
constexpr uint8 kProtocolUdp = 17;

// Default Hop Limit for test packets.
constexpr uint8 kTestHopLimit = 64;

// Builds a simple IPv6 UDP datagram for testing MIPv6 tunneling and route
// optimization.
std::string MakeTestIpv6Datagram(const IpAddress& src, const IpAddress& dst,
                                 std::string_view payload) {
  Ipv6Datagram dgram;
  dgram.source = src;
  dgram.destination = dst;
  dgram.next_header = kProtocolUdp;
  dgram.hop_limit = kTestHopLimit;
  dgram.payload.assign(payload);
  return SerializeIpv6Datagram(dgram);
}

}  // namespace

TEST(Mip6MobilityHeaderRoundTripAllMessageTypes) {
  IpAddress src = *IpAddress::Parse("2001:db8:1::10");
  IpAddress dst = *IpAddress::Parse("2001:db8:2::20");

  // Binding Refresh Request (type 0).
  MobilityMessage brr;
  brr.type = MobilityHeaderType::BindingRefreshRequest;
  std::string brr_wire = SerializeMobilityHeader(src, dst, brr);
  auto brr_parsed = ParseMobilityHeader(src, dst, brr_wire);
  EXPECT(true, brr_parsed.has_value());
  EXPECT(MobilityHeaderType::BindingRefreshRequest, brr_parsed->type);

  // Home Test Init (type 1) and Care-of Test Init (type 2).
  MobilityMessage hoti;
  hoti.type = MobilityHeaderType::HomeTestInit;
  hoti.init_cookie = {1, 2, 3, 4, 5, 6, 7, 8};
  auto hoti_parsed =
      ParseMobilityHeader(src, dst, SerializeMobilityHeader(src, dst, hoti));
  EXPECT(true, hoti_parsed.has_value());
  EXPECT(MobilityHeaderType::HomeTestInit, hoti_parsed->type);
  EXPECT(true, hoti_parsed->init_cookie == hoti.init_cookie);

  // Home Test (type 3) and Care-of Test (type 4).
  MobilityMessage cot;
  cot.type = MobilityHeaderType::CareOfTest;
  cot.nonce_index = 42;
  cot.init_cookie = {8, 7, 6, 5, 4, 3, 2, 1};
  cot.keygen_token = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
  auto cot_parsed =
      ParseMobilityHeader(src, dst, SerializeMobilityHeader(src, dst, cot));
  EXPECT(true, cot_parsed.has_value());
  EXPECT(MobilityHeaderType::CareOfTest, cot_parsed->type);
  EXPECT((uint16)42, cot_parsed->nonce_index);
  EXPECT(true, cot_parsed->keygen_token == cot.keygen_token);

  // Binding Update (type 5) with Alternate Care-of Address, Nonce Indices, and
  // Binding Authorization Data.
  std::array<uint8, 8> h_tok = {1, 2, 3, 4, 5, 6, 7, 8};
  std::array<uint8, 8> c_tok = {9, 10, 11, 12, 13, 14, 15, 16};
  auto k_bm = DeriveBindingManagementKey(h_tok, c_tok);

  MobilityMessage bu;
  bu.type = MobilityHeaderType::BindingUpdate;
  bu.sequence_number = 77;
  bu.acknowledge_requested = true;
  bu.home_registration = false;
  bu.lifetime_units = 105;
  bu.alternate_care_of_address = *IpAddress::Parse("2001:db8:99::5");
  bu.nonce_indices = std::make_pair<uint16, uint16>(1, 1);
  bu.authenticator = std::array<uint8, 12>{};

  std::string bu_wire = SerializeMobilityHeader(
      src, dst, bu, k_bm, *bu.alternate_care_of_address, dst);
  auto bu_parsed = ParseMobilityHeader(src, dst, bu_wire);
  EXPECT(true, bu_parsed.has_value());
  EXPECT(MobilityHeaderType::BindingUpdate, bu_parsed->type);
  EXPECT((uint16)77, bu_parsed->sequence_number);
  EXPECT(true, bu_parsed->acknowledge_requested);
  EXPECT(true,
         bu_parsed->alternate_care_of_address == bu.alternate_care_of_address);
  EXPECT(true, VerifyMobilityHeaderAuthenticator(
                   bu_wire, k_bm, *bu.alternate_care_of_address, dst));

  // Binding Error (type 7).
  MobilityMessage be;
  be.type = MobilityHeaderType::BindingError;
  be.status =
      static_cast<uint8>(BindingErrorStatus::UnknownBindingForHomeAddressOption);
  be.error_home_address = src;
  auto be_parsed =
      ParseMobilityHeader(dst, src, SerializeMobilityHeader(dst, src, be));
  EXPECT(true, be_parsed.has_value());
  EXPECT(MobilityHeaderType::BindingError, be_parsed->type);
  EXPECT(true, be_parsed->error_home_address == src);

  // Corrupted checksum is rejected.
  bu_wire[5] ^= 0xFF;
  EXPECT(false, ParseMobilityHeader(src, dst, bu_wire).has_value());
}

TEST(Mip6Type2RoutingHeaderAllowedWhileType0Rejected) {
  IpAddress cn = *IpAddress::Parse("2001:db8:2::1");
  IpAddress coa = *IpAddress::Parse("2001:db8:99::5");
  IpAddress hoa = *IpAddress::Parse("2001:db8:1::10");

  std::string rh2 = BuildType2RoutingHeader(kProtocolUdp, hoa);
  Ipv6Header hdr;
  hdr.source = cn;
  hdr.destination = coa;
  hdr.next_header = static_cast<uint8>(Ipv6NextHeader::Routing);
  hdr.hop_limit = kTestHopLimit;
  hdr.payload_length = static_cast<uint16>(rh2.size() + 4);

  std::string pkt;
  AppendIpv6Header(hdr, pkt);
  pkt.append(rh2);
  pkt.append("ping");

  uint32 problem_ptr = 0;
  EXPECT(RoutingHeaderStatus::ProcessedType2,
         ProcessMip6RoutingHeader(pkt, kIpv6HeaderSize, hoa, problem_ptr));

  auto updated_hdr = ParseIpv6Header(pkt);
  EXPECT(true, updated_hdr.has_value());
  EXPECT(true, updated_hdr->destination == hoa);
  EXPECT((uint8)0, static_cast<uint8>(pkt[kIpv6HeaderSize + 3]));

  // Construct a Type 0 Routing Header with Segments Left = 1 and verify it is
  // rejected with Parameter Problem pointing at the Routing Type field.
  std::string rh0_pkt = pkt;
  rh0_pkt[kIpv6HeaderSize + 2] = 0;  // Routing Type 0
  rh0_pkt[kIpv6HeaderSize + 3] = 1;  // Segments Left = 1
  uint32 rh0_problem_ptr = 0;
  EXPECT(RoutingHeaderStatus::RejectedParameterProblem,
         ProcessMip6RoutingHeader(rh0_pkt, kIpv6HeaderSize, hoa,
                                  rh0_problem_ptr));
  EXPECT((uint32)(kIpv6HeaderSize + 2), rh0_problem_ptr);
}

TEST(Mip6HomeAgentRegistrationProxyNdpAndBidirectionalTunneling) {
  auto now = std::chrono::steady_clock::now();
  IpAddress home_prefix = *IpAddress::Parse("2001:db8:1::");
  IpAddress ha_addr = *IpAddress::Parse("2001:db8:1::1");
  IpAddress mn_hoa = *IpAddress::Parse("2001:db8:1::10");
  IpAddress mn_coa = *IpAddress::Parse("2001:db8:99::55");
  IpAddress cn_addr = *IpAddress::Parse("2001:db8:2::20");

  Mipv6HomeAgent ha(ha_addr, home_prefix, 64);
  Mipv6MobileNode mn(mn_hoa, ha_addr);

  mn.SetCareOfAddress(mn_coa);
  EXPECT(true, mn.IsAwayFromHome());
  EXPECT(false, mn.is_home_registered());

  // Mobile Node builds a Home Agent Binding Update (H=1, A=1).
  std::string bu_pkt = mn.BuildHomeAgentBindingUpdate(/*lifetime_units=*/300);
  auto bu_ip6 = ParseIpv6Header(bu_pkt);
  EXPECT(true, bu_ip6.has_value());
  EXPECT(true, bu_ip6->source == mn_coa);
  EXPECT(true, bu_ip6->destination == ha_addr);

  // Extract Destination Options (Home Address 201) and Mobility Header.
  auto hoa_opt = ParseHomeAddressDestinationOption(
      std::string_view(bu_pkt).substr(kIpv6HeaderSize + 2, 22));
  EXPECT(true, hoa_opt.has_value());
  EXPECT(true, *hoa_opt == mn_hoa);

  auto bu_msg = ParseMobilityHeader(
      mn_hoa, ha_addr, std::string_view(bu_pkt).substr(kIpv6HeaderSize + 24));
  EXPECT(true, bu_msg.has_value());

  // Home Agent processes the Binding Update, records Proxy NDP targets, and
  // returns a Binding Acknowledgement routed via Type 2 Routing Header.
  auto ba_pkt = ha.HandleBindingUpdate(mn_coa, hoa_opt, *bu_msg, now);
  EXPECT(true, ba_pkt.has_value());
  EXPECT(true, ha.ShouldProxyNdpFor(mn_hoa, now));
  EXPECT(true, ha.ShouldProxyNdpFor(SolicitedNodeMulticastAddress(mn_hoa), now));
  EXPECT(true, mn.HandleHomeAgentBindingAck(*ba_pkt));
  EXPECT(true, mn.is_home_registered());

  // Packet from CN to MN's Home Address is intercepted by HA and tunneled to
  // MN's Care-of Address; MN decapsulates it back to the original inner packet.
  std::string cn_to_mn = MakeTestIpv6Datagram(cn_addr, mn_hoa, "hello-mn");
  auto tunneled_to_mn = ha.InterceptAndTunnelPacket(cn_to_mn, now);
  EXPECT(true, tunneled_to_mn.has_value());

  auto decapped_at_mn = mn.ProcessInboundPacket(*tunneled_to_mn);
  EXPECT(true, decapped_at_mn.has_value());
  EXPECT(cn_to_mn, *decapped_at_mn);

  // Packet from MN to CN (before route optimization) is reverse-tunneled to HA,
  // and HA decapsulates it for delivery to CN.
  std::string mn_to_cn = MakeTestIpv6Datagram(mn_hoa, cn_addr, "hello-cn");
  std::string reverse_tunneled = mn.PrepareOutboundPacket(mn_to_cn);
  auto decapped_at_ha =
      ha.DecapsulateReverseTunnelPacket(reverse_tunneled, now);
  EXPECT(true, decapped_at_ha.has_value());
  EXPECT(mn_to_cn, *decapped_at_ha);
}

TEST(Mip6ReturnRoutabilityAndCorrespondentNodeRouteOptimization) {
  auto now = std::chrono::steady_clock::now();
  IpAddress ha_addr = *IpAddress::Parse("2001:db8:1::1");
  IpAddress mn_hoa = *IpAddress::Parse("2001:db8:1::10");
  IpAddress mn_coa = *IpAddress::Parse("2001:db8:99::55");
  IpAddress cn_addr = *IpAddress::Parse("2001:db8:2::20");

  std::array<uint8, 16> cn_key = {1, 2, 3, 4, 5, 6, 7, 8,
                                  9, 10, 11, 12, 13, 14, 15, 16};
  Mipv6CorrespondentNode cn(cn_addr, cn_key);
  Mipv6HomeAgent ha(ha_addr, *IpAddress::Parse("2001:db8:1::"), 64);
  Mipv6MobileNode mn(mn_hoa, ha_addr);
  mn.SetCareOfAddress(mn_coa);

  // Register MN with HA first so HA can decapsulate HoTI.
  std::string ha_bu_pkt = mn.BuildHomeAgentBindingUpdate();
  auto ha_bu_msg = ParseMobilityHeader(
      mn_hoa, ha_addr,
      std::string_view(ha_bu_pkt).substr(kIpv6HeaderSize + 24));
  ha.HandleBindingUpdate(mn_coa, mn_hoa, *ha_bu_msg, now);

  // Before binding, a packet with Home Address Destination Option triggers a
  // Binding Error (status 1) at CN.
  std::string premature_opt_hdr =
      BuildHomeAddressDestinationOptionsHeader(kProtocolUdp, mn_hoa);
  Ipv6Header prem_ip6;
  prem_ip6.source = mn_coa;
  prem_ip6.destination = cn_addr;
  prem_ip6.next_header =
      static_cast<uint8>(Ipv6NextHeader::DestinationOptions);
  prem_ip6.hop_limit = kTestHopLimit;
  prem_ip6.payload_length = static_cast<uint16>(premature_opt_hdr.size() + 4);
  std::string premature_pkt;
  AppendIpv6Header(prem_ip6, premature_pkt);
  premature_pkt.append(premature_opt_hdr);
  premature_pkt.append("data");

  std::string normalized;
  std::optional<std::string> binding_error_pkt;
  EXPECT(false, cn.ProcessInboundPacket(premature_pkt, now, normalized,
                                        binding_error_pkt));
  EXPECT(true, binding_error_pkt.has_value());

  // MN initiates Return Routability (HoTI via HA + CoTI direct).
  auto [hoti_tunneled, coti_direct] = mn.InitiateReturnRoutability(
      cn_addr, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88},
      {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11});

  auto hoti_inner = ha.DecapsulateReverseTunnelPacket(hoti_tunneled, now);
  EXPECT(true, hoti_inner.has_value());

  auto hot_pkt = cn.HandleMobilityPacket(
      mn_hoa, cn_addr, std::nullopt,
      std::string_view(*hoti_inner).substr(kIpv6HeaderSize), now);
  EXPECT(true, hot_pkt.has_value());

  auto cot_pkt = cn.HandleMobilityPacket(
      mn_coa, cn_addr, std::nullopt,
      std::string_view(coti_direct).substr(kIpv6HeaderSize), now);
  EXPECT(true, cot_pkt.has_value());

  // MN processes HoT and CoT, producing a signed CN Binding Update.
  auto hot_msg = ParseMobilityHeader(
      cn_addr, mn_hoa, std::string_view(*hot_pkt).substr(kIpv6HeaderSize));
  auto cot_msg = ParseMobilityHeader(
      cn_addr, mn_coa, std::string_view(*cot_pkt).substr(kIpv6HeaderSize));
  EXPECT(true, hot_msg.has_value());
  EXPECT(true, cot_msg.has_value());

  EXPECT(false,
         mn.HandleReturnRoutabilityResponse(cn_addr, *hot_msg).has_value());
  auto cn_bu_pkt = mn.HandleReturnRoutabilityResponse(cn_addr, *cot_msg);
  EXPECT(true, cn_bu_pkt.has_value());

  // CN processes the authenticated Binding Update and populates its Binding
  // Cache.
  auto cn_ba_pkt = cn.HandleMobilityPacket(
      mn_coa, cn_addr, mn_hoa,
      std::string_view(*cn_bu_pkt).substr(kIpv6HeaderSize + 24), now);
  EXPECT(true, cn_ba_pkt.has_value());
  auto cached = cn.LookupBinding(mn_hoa, now);
  EXPECT(true, cached.has_value());
  EXPECT(true, cached->care_of_address == mn_coa);

  mn.ConfirmCorrespondentBinding(cn_addr);

  // Route-optimized packet from MN -> CN uses Home Address Destination Option
  // (201) and is normalized by CN back to source == mn_hoa.
  std::string inner_mn_to_cn = MakeTestIpv6Datagram(mn_hoa, cn_addr, "ro-up");
  std::string ro_mn_to_cn = mn.PrepareOutboundPacket(inner_mn_to_cn);
  EXPECT(true, cn.ProcessInboundPacket(ro_mn_to_cn, now, normalized,
                                       binding_error_pkt));
  EXPECT(inner_mn_to_cn, normalized);

  // Route-optimized packet from CN -> MN uses Type 2 Routing Header and is
  // processed by MN back to destination == mn_hoa.
  std::string inner_cn_to_mn = MakeTestIpv6Datagram(cn_addr, mn_hoa, "ro-down");
  std::string ro_cn_to_mn = cn.TransformOutboundPacket(inner_cn_to_mn, now);
  auto mn_received = mn.ProcessInboundPacket(ro_cn_to_mn);
  EXPECT(true, mn_received.has_value());
  auto mn_rx_hdr = ParseIpv6Header(*mn_received);
  EXPECT(true, mn_rx_hdr.has_value());
  EXPECT(true, mn_rx_hdr->destination == mn_hoa);
}
