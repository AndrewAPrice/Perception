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

#include <string>

#include "ikev2.h"
#include "ipsec.h"
#include "ipv6_header.h"
#include "testing.h"
#include "wire_format.h"

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

namespace {

// UDP protocol number.
constexpr uint8 kProtocolUdp = 17;

// Default Hop Limit / TTL.
constexpr uint8 kDefaultTtl = 64;

// Builds a minimal IPv6 UDP packet for IPsec testing.
std::string MakeIpv6UdpPacket(const IpAddress& src, const IpAddress& dst,
                              uint16 src_port, uint16 dst_port,
                              std::string_view payload) {
  std::string udp;
  WireWriter w(udp);
  w.WriteU16(src_port);
  w.WriteU16(dst_port);
  w.WriteU16(static_cast<uint16>(8 + payload.size()));
  w.WriteU16(0xFFFF);
  w.WriteBytes(payload);

  Ipv6Datagram dgram;
  dgram.source = src;
  dgram.destination = dst;
  dgram.next_header = kProtocolUdp;
  dgram.hop_limit = kDefaultTtl;
  dgram.payload = std::move(udp);
  return SerializeIpv6Datagram(dgram);
}

// Builds a minimal IPv4 UDP packet for IPsec testing.
std::string MakeIpv4UdpPacket(const IpAddress& src, const IpAddress& dst,
                              uint16 src_port, uint16 dst_port,
                              std::string_view payload) {
  std::string udp;
  WireWriter uw(udp);
  uw.WriteU16(src_port);
  uw.WriteU16(dst_port);
  uw.WriteU16(static_cast<uint16>(8 + payload.size()));
  uw.WriteU16(0);
  uw.WriteBytes(payload);

  uint16 total_len = static_cast<uint16>(20 + udp.size());
  std::string out;
  WireWriter ipw(out);
  ipw.WriteU8(0x45);
  ipw.WriteU8(0);
  ipw.WriteU16(total_len);
  ipw.WriteU16(0x1111);
  ipw.WriteU16(0);
  ipw.WriteU8(kDefaultTtl);
  ipw.WriteU8(kProtocolUdp);
  ipw.WriteU16(0);
  for (size_t i = 0; i < 4; i++) ipw.WriteU8(src.bytes()[i]);
  for (size_t i = 0; i < 4; i++) ipw.WriteU8(dst.bytes()[i]);
  ipw.WriteBytes(udp);
  return out;
}

}  // namespace

TEST(IpsecAntiReplaySlidingWindow) {
  AntiReplayWindow window;
  EXPECT(false, window.Check(0));
  EXPECT(true, window.Check(1));
  window.Advance(1);
  EXPECT(false, window.Check(1));  // Duplicate rejected.

  EXPECT(true, window.Check(64));
  window.Advance(64);
  EXPECT(false, window.Check(64));
  EXPECT(true, window.Check(2));
  window.Advance(2);
  EXPECT(false, window.Check(2));

  // Advance to 100; sequence 20 is outside the 64-packet window (100 - 20 >= 64).
  EXPECT(true, window.Check(100));
  window.Advance(100);
  EXPECT(false, window.Check(20));
  EXPECT(true, window.Check(50));
  window.Advance(50);
  EXPECT(false, window.Check(50));
}

TEST(IpsecEspTransportAndTunnelModeAesGcmAndChaCha20Poly1305) {
  IpsecEngine tx_engine;
  IpsecEngine rx_engine;

  IpAddress v6_src = *IpAddress::Parse("2001:db8:1::10");
  IpAddress v6_dst = *IpAddress::Parse("2001:db8:2::20");
  IpAddress v4_src = IpAddress::V4(10, 0, 1, 10);
  IpAddress v4_dst = IpAddress::V4(10, 0, 2, 20);

  // Install AES-128-GCM Transport-mode SA for IPv6 (SPI 0x1001) and
  // ChaCha20-Poly1305 Tunnel-mode SA for IPv4 (SPI 0x2002).
  SecurityAssociation sa_v6_transport;
  sa_v6_transport.spi = 0x1001;
  sa_v6_transport.mode = IpsecMode::Transport;
  sa_v6_transport.cipher = EspCipherSuite::Aes128Gcm16;
  for (size_t i = 0; i < 32; i++) sa_v6_transport.key[i] = static_cast<uint8>(i + 1);
  sa_v6_transport.salt = {0xAA, 0xBB, 0xCC, 0xDD};
  tx_engine.InstallSa(sa_v6_transport);
  rx_engine.InstallSa(sa_v6_transport);

  SecurityAssociation sa_v4_tunnel;
  sa_v4_tunnel.spi = 0x2002;
  sa_v4_tunnel.mode = IpsecMode::Tunnel;
  sa_v4_tunnel.cipher = EspCipherSuite::ChaCha20Poly1305;
  for (size_t i = 0; i < 32; i++) sa_v4_tunnel.key[i] = static_cast<uint8>(0x80 + i);
  sa_v4_tunnel.salt = {0x11, 0x22, 0x33, 0x44};
  sa_v4_tunnel.tunnel_local = IpAddress::V4(198, 51, 100, 1);
  sa_v4_tunnel.tunnel_remote = IpAddress::V4(203, 0, 113, 1);
  tx_engine.InstallSa(sa_v4_tunnel);
  rx_engine.InstallSa(sa_v4_tunnel);

  SpdRule protect_v6;
  protect_v6.family = IpAddressFamily::V6;
  protect_v6.action = SpdAction::Protect;
  protect_v6.mode = IpsecMode::Transport;
  protect_v6.sa_spi = 0x1001;
  tx_engine.AddSpdRule(protect_v6);
  rx_engine.AddSpdRule(protect_v6);

  SpdRule protect_v4;
  protect_v4.family = IpAddressFamily::V4;
  protect_v4.action = SpdAction::Protect;
  protect_v4.mode = IpsecMode::Tunnel;
  protect_v4.sa_spi = 0x2002;
  tx_engine.AddSpdRule(protect_v4);
  rx_engine.AddSpdRule(protect_v4);

  // IPv6 Transport mode AES-128-GCM round-trip.
  std::string orig_v6 = MakeIpv6UdpPacket(v6_src, v6_dst, 1234, 5678, "secret-v6");
  IpsecResult enc_v6 = tx_engine.ProcessOutbound(orig_v6);
  EXPECT(IpsecStatus::Protected, enc_v6.status);
  EXPECT((uint32)0x1001, enc_v6.spi);

  IpsecResult dec_v6 = rx_engine.ProcessInbound(enc_v6.packet);
  EXPECT(IpsecStatus::Protected, dec_v6.status);
  EXPECT(orig_v6, dec_v6.packet);

  // Replay of the exact same ESP packet is dropped by the anti-replay window.
  EXPECT(IpsecStatus::Dropped, rx_engine.ProcessInbound(enc_v6.packet).status);

  // IPv4 Tunnel mode ChaCha20-Poly1305 round-trip.
  std::string orig_v4 = MakeIpv4UdpPacket(v4_src, v4_dst, 4000, 80, "secret-v4");
  IpsecResult enc_v4 = tx_engine.ProcessOutbound(orig_v4);
  EXPECT(IpsecStatus::Protected, enc_v4.status);
  IpsecResult dec_v4 = rx_engine.ProcessInbound(enc_v4.packet);
  EXPECT(IpsecStatus::Protected, dec_v4.status);
  EXPECT(orig_v4, dec_v4.packet);
}

TEST(Ikev2PskEapAndMobikeStateMachine) {
  IpAddress init_addr = *IpAddress::Parse("2001:db8:1::10");
  IpAddress resp_addr = *IpAddress::Parse("2001:db8:2::1");
  IpAddress new_init_addr = *IpAddress::Parse("2001:db8:99::77");

  IpsecEngine init_ipsec;
  IpsecEngine resp_ipsec;

  Ikev2Session initiator(true, 0x1122334455667788ULL, init_addr, resp_addr);
  Ikev2Session responder(false, 0xAABBCCDDEEFF0011ULL, resp_addr, init_addr);

  // IKE_SA_INIT exchange.
  IkeMessage sa_init_req = initiator.BuildSaInitRequest("dh-init", "nonce-i");
  std::string wire_init_req = SerializeIkeMessage(sa_init_req);
  auto parsed_init_req = ParseIkeMessage(wire_init_req);
  EXPECT(true, parsed_init_req.has_value());

  auto sa_init_resp = responder.HandleSaInitRequest(
      *parsed_init_req, "dh-resp", "nonce-r", "dh-shared-secret");
  EXPECT(true, sa_init_resp.has_value());
  EXPECT(true,
         initiator.HandleSaInitResponse(*sa_init_resp, "dh-shared-secret"));

  // Multi-round IKE_AUTH with EAP and MOBIKE_SUPPORTED negotiation.
  IkeMessage auth_req1 = initiator.BuildAuthRequest(
      "client@perception", IkeAuthMethod::SharedKeyMessageIntegrityCode, "",
      /*child_spi=*/0x3001, /*use_eap=*/true);
  auto auth_resp1 = responder.HandleAuthRequest(
      auth_req1, "vpn@perception", IkeAuthMethod::RsaDigitalSignature,
      "server-cert", /*responder_child_spi=*/0x3002, resp_ipsec,
      /*require_eap=*/true, "eap-challenge-123");
  EXPECT(true, auth_resp1.has_value());
  EXPECT(IkeSessionState::EapInProgress, responder.state());

  // Initiator answers EAP challenge.
  auto eap_req2 = initiator.HandleEapStepOnInitiator(
      *auth_resp1, "eap-msk-secret", "client@perception");
  EXPECT(true, eap_req2.has_value());

  // Responder validates EAP response and sends EAP-Success.
  auto eap_resp2 = responder.HandleEapStepOnResponder(
      *eap_req2, "eap-msk-secret", "vpn@perception", 0x3002, resp_ipsec);
  EXPECT(true, eap_resp2.has_value());

  // Initiator sends MSK-derived AUTH payload; Responder verifies and installs
  // Child SAs.
  auto eap_req3 = initiator.HandleEapStepOnInitiator(
      *eap_resp2, "eap-msk-secret", "client@perception");
  EXPECT(true, eap_req3.has_value());
  auto final_auth_resp = responder.HandleEapStepOnResponder(
      *eap_req3, "eap-msk-secret", "vpn@perception", 0x3002, resp_ipsec);
  EXPECT(true, final_auth_resp.has_value());
  EXPECT(IkeSessionState::Established, responder.state());
  EXPECT(true, initiator.HandleFinalAuthResponse(*final_auth_resp,
                                                 "eap-msk-secret", init_ipsec));
  EXPECT(IkeSessionState::Established, initiator.state());
  EXPECT(true, initiator.mobike_enabled());
  EXPECT(true, responder.mobike_enabled());

  // RFC 4555 MOBIKE: Initiator roams to `new_init_addr`, sends
  // UPDATE_SA_ADDRESSES + COOKIE2, and both sides update their outer tunnel
  // endpoints.
  std::array<uint8, 8> cookie2 = {0xDE, 0xAD, 0xBE, 0xEF, 1, 2, 3, 4};
  IkeMessage mobike_req =
      initiator.InitiateMobikeAddressUpdate(new_init_addr, cookie2, init_ipsec);
  auto mobike_resp = responder.HandleInformationalRequest(
      mobike_req, new_init_addr, resp_addr, resp_ipsec);
  EXPECT(true, mobike_resp.has_value());
  EXPECT(true, initiator.HandleInformationalResponse(*mobike_resp));
  EXPECT(IkeSessionState::Established, initiator.state());
  EXPECT(true, responder.remote_address() == new_init_addr);
  EXPECT(true,
         resp_ipsec.FindSa(0x3002)->tunnel_remote == new_init_addr);
  EXPECT(true,
         init_ipsec.FindSa(0x3001)->tunnel_local == new_init_addr);
}
