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

#include "tcp_isn.h"

#include <array>
#include <chrono>
#include <vector>

#include "testing.h"

namespace {

using std::chrono::microseconds;
using std::chrono::seconds;

// Official SipHash-2-4 test vector output for 0 bytes with key 00..0f.
constexpr uint64 kSipHashVectorEmpty = 0x726fdb47dd0e0e31ULL;

// Official SipHash-2-4 test vector output for 15 bytes 00..0e with key 00..0f.
constexpr uint64 kSipHashVector15Bytes = 0xa129ca6149be45e5ULL;

// Standard Ethernet link MTU.
constexpr uint16 kEthernetMtu = 1500;

// Expected IPv4 MSS on a 1500-byte link.
constexpr uint16 kExpectedIpv4Mss = 1460;

// Expected IPv6 MSS on a 1500-byte link.
constexpr uint16 kExpectedIpv6Mss = 1440;

// Default IPv4 send MSS when peer omits the MSS option (RFC 9293 §3.7.1).
constexpr uint16 kDefaultIpv4Mss = 536;

// Default IPv6 send MSS when peer omits the MSS option (RFC 9293 §3.7.1).
constexpr uint16 kDefaultIpv6Mss = 1220;

// Sample local port for ISN tests.
constexpr uint16 kLocalPortA = 49152;

// Alternate local port for ISN tests.
constexpr uint16 kLocalPortB = 49153;

// Sample remote port for ISN tests.
constexpr uint16 kRemotePortHttp = 80;

// Sample RCV.NXT for validation tests.
constexpr uint32 kTestRcvNxt = 5000;

// Sample RCV.WND for validation tests.
constexpr uint32 kTestRcvWnd = 4096;

SipHashKey MakeSequentialKey() {
  SipHashKey key{};
  for (size_t i = 0; i < key.size(); i++) key[i] = static_cast<uint8>(i);
  return key;
}

TEST(SipHash24_OfficialVectors) {
  SipHashKey key = MakeSequentialKey();
  std::span<const uint8> empty;
  EXPECT(kSipHashVectorEmpty, SipHash24(key, empty));

  std::array<uint8, 15> msg{};
  for (size_t i = 0; i < msg.size(); i++) msg[i] = static_cast<uint8>(i);
  EXPECT(kSipHashVector15Bytes, SipHash24(key, msg));
}

TEST(TcpIsn_MonotonicPerTupleAndDistinctAcrossTuples) {
  TcpIsnGenerator gen(MakeSequentialKey());
  std::array<uint8, 4> local_v4 = {10, 0, 2, 15};
  std::array<uint8, 4> remote_v4 = {93, 184, 216, 34};

  TcpTime t0 = microseconds(100);
  TcpTime t1 = microseconds(104);  // +1 tick of 4 us
  TcpTime t2 = microseconds(140);  // +10 ticks of 4 us

  uint32 isn0 =
      gen.Generate(local_v4, kLocalPortA, remote_v4, kRemotePortHttp, t0);
  uint32 isn1 =
      gen.Generate(local_v4, kLocalPortA, remote_v4, kRemotePortHttp, t1);
  uint32 isn2 =
      gen.Generate(local_v4, kLocalPortA, remote_v4, kRemotePortHttp, t2);

  EXPECT(1U, isn1 - isn0);
  EXPECT(10U, isn2 - isn0);
  EXPECT(true, SeqLess(isn0, isn1));
  EXPECT(true, SeqLess(isn1, isn2));

  // Changing any element of the 4-tuple produces an unrelated ISN offset.
  uint32 diff_port =
      gen.Generate(local_v4, kLocalPortB, remote_v4, kRemotePortHttp, t0);
  EXPECT(true, diff_port != isn0);

  std::array<uint8, 16> local_v6 = {0xfe, 0xc0, 0, 0, 0, 0, 0, 0,
                                    0x50, 0x54, 0, 0, 0, 0, 0, 1};
  std::array<uint8, 16> remote_v6 = {0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0, 0,
                                     0,    0,    0,    0,    0,    0,    0x88, 0x88};
  uint32 v6_isn0 =
      gen.Generate(local_v6, kLocalPortA, remote_v6, kRemotePortHttp, t0);
  uint32 v6_isn1 =
      gen.Generate(local_v6, kLocalPortA, remote_v6, kRemotePortHttp, t1);
  EXPECT(1U, v6_isn1 - v6_isn0);
  EXPECT(true, v6_isn0 != isn0);
}

TEST(TcpOptions_EncodeAndParseMss) {
  EXPECT(kExpectedIpv4Mss, MssForMtu(kEthernetMtu, TcpNetworkLayer::kIpv4));
  EXPECT(kExpectedIpv6Mss, MssForMtu(kEthernetMtu, TcpNetworkLayer::kIpv6));

  std::string raw = EncodeMssOption(kExpectedIpv6Mss);
  ASSERT(static_cast<size_t>(4), raw.size());
  std::span<const uint8> span(reinterpret_cast<const uint8*>(raw.data()),
                              raw.size());
  auto parsed = ParseTcpOptions(span);
  ASSERT(true, parsed.has_value());
  ASSERT(true, parsed->mss.has_value());
  EXPECT(kExpectedIpv6Mss, *parsed->mss);

  // NOP padding + unknown option + MSS + End-of-Option-List.
  std::vector<uint8> complex_opts = {
      1,                    // NOP
      8, 4, 0xAA, 0xBB,     // Unknown kind 8, length 4
      2, 4, 0x05, 0x00,     // MSS = 1280
      0,                    // EOL
      2, 4, 0x01, 0x00      // Ignored after EOL
  };
  auto parsed_complex = ParseTcpOptions(complex_opts);
  ASSERT(true, parsed_complex.has_value());
  ASSERT(true, parsed_complex->mss.has_value());
  EXPECT(static_cast<uint16>(1280), *parsed_complex->mss);

  // Malformed options (length < 2 or truncated) are rejected.
  std::vector<uint8> bad_len = {2, 1, 0, 0};
  EXPECT(false, ParseTcpOptions(bad_len).has_value());
  std::vector<uint8> truncated = {2, 4, 0x05};
  EXPECT(false, ParseTcpOptions(truncated).has_value());

  // EffectiveSendMss honors min(peer_mss, pmtu - headers) and defaults.
  EXPECT(kDefaultIpv4Mss,
         EffectiveSendMss(std::nullopt, kEthernetMtu, TcpNetworkLayer::kIpv4));
  EXPECT(kDefaultIpv6Mss,
         EffectiveSendMss(std::nullopt, kEthernetMtu, TcpNetworkLayer::kIpv6));
  EXPECT(static_cast<uint16>(1220),
         EffectiveSendMss(1440, 1280, TcpNetworkLayer::kIpv6));
  EXPECT(static_cast<uint16>(1200),
         EffectiveSendMss(1200, kEthernetMtu, TcpNetworkLayer::kIpv4));
}

TEST(TcpValidation_RstAndAcceptabilityAndResetReply) {
  // RFC 5961 §3 in-window RST validation.
  EXPECT(static_cast<int>(RstAction::kReset),
         static_cast<int>(ValidateRst(kTestRcvNxt, kTestRcvNxt, kTestRcvWnd)));
  EXPECT(static_cast<int>(RstAction::kChallengeAck),
         static_cast<int>(
             ValidateRst(kTestRcvNxt + 100, kTestRcvNxt, kTestRcvWnd)));
  EXPECT(static_cast<int>(RstAction::kDrop),
         static_cast<int>(
             ValidateRst(kTestRcvNxt - 1, kTestRcvNxt, kTestRcvWnd)));
  EXPECT(static_cast<int>(RstAction::kDrop),
         static_cast<int>(ValidateRst(kTestRcvNxt + kTestRcvWnd, kTestRcvNxt,
                                      kTestRcvWnd)));

  // SYN-SENT RST acceptability requires an ACK of the SYN.
  TcpSegmentInfo rst_with_ack{
      .sequence = 0, .acknowledgment = 1001, .ack = true, .rst = true};
  EXPECT(true, IsAcceptableRstInSynSent(rst_with_ack, 1000, 1001));
  rst_with_ack.acknowledgment = 1000;
  EXPECT(false, IsAcceptableRstInSynSent(rst_with_ack, 1000, 1001));

  // BuildResetReply never responds to a RST, and distinguishes ACK vs non-ACK.
  EXPECT(false, BuildResetReply(rst_with_ack).has_value());

  TcpSegmentInfo unmatched_syn{.sequence = 300, .syn = true};
  auto reply_syn = BuildResetReply(unmatched_syn);
  ASSERT(true, reply_syn.has_value());
  EXPECT(0U, reply_syn->sequence);
  EXPECT(301U, reply_syn->acknowledgment);
  EXPECT(true, reply_syn->ack);

  TcpSegmentInfo unmatched_ack{
      .sequence = 300, .acknowledgment = 777, .ack = true};
  auto reply_ack = BuildResetReply(unmatched_ack);
  ASSERT(true, reply_ack.has_value());
  EXPECT(777U, reply_ack->sequence);
  EXPECT(false, reply_ack->ack);
}

TEST(TcpTimeWait_DurationAndFinRestart) {
  TcpTime t0 = seconds(10);
  TcpTimeWait tw(t0);
  EXPECT(microseconds(seconds(60)).count(), TcpTimeWait::Duration().count());
  EXPECT(microseconds(seconds(70)).count(), tw.Deadline().count());
  EXPECT(false, tw.IsExpired(seconds(69)));
  EXPECT(true, tw.IsExpired(seconds(70)));

  // Non-FIN segment does not restart the 2*MSL timer; FIN does.
  EXPECT(false, tw.OnSegment(false, seconds(50)));
  EXPECT(microseconds(seconds(70)).count(), tw.Deadline().count());
  EXPECT(true, tw.OnSegment(true, seconds(50)));
  EXPECT(microseconds(seconds(110)).count(), tw.Deadline().count());
}

}  // namespace
