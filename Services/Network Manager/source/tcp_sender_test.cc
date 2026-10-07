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

#include "tcp_sender.h"

#include <chrono>
#include <string>
#include <vector>

#include "testing.h"

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

// Initial sequence number used in sender tests.
constexpr uint32 kTestIss = 1000;

// Test MSS for easy arithmetic.
constexpr uint16 kTestMss = 1000;

// Large peer receive window that does not constrain cwnd.
constexpr uint32 kLargeWindow = 65535;

// Captured copy of a transmitted segment for test assertions.
struct SentSegment {
  // Sequence number of the segment.
  uint32 sequence = 0;
  // Owned copy of the payload bytes.
  std::string payload;
  // SYN flag.
  bool syn = false;
  // FIN flag.
  bool fin = false;
  // Whether the sender marked the segment as a retransmission.
  bool retransmission = false;
};

// Test harness owning a TcpSender and recording all emitted segments.
struct SenderHarness {
  // Recorded outgoing segments.
  std::vector<SentSegment> sent;
  // Sender under test.
  TcpSender sender;

  explicit SenderHarness(uint16 mss = kTestMss)
      : sender(kTestIss, TcpSenderOptions::Default(mss),
               [this](const TcpOutgoingSegment& seg) {
                 sent.push_back(SentSegment{
                     .sequence = seg.sequence,
                     .payload = std::string(seg.payload),
                     .syn = seg.syn,
                     .fin = seg.fin,
                     .retransmission = seg.retransmission});
               }) {}

  // Completes the 3-way handshake at `rtt` with `window`.
  void Establish(TcpTime rtt = milliseconds(100),
                 uint32 window = kLargeWindow) {
    sender.SendSyn(seconds(0));
    sent.clear();
    TcpSegmentInfo syn_ack{
        .sequence = 5000, .acknowledgment = kTestIss + 1, .syn = true, .ack = true};
    sender.OnSegment(syn_ack, window, rtt);
  }
};

TEST(TcpSender_HandshakeAndInitialWindow) {
  SenderHarness h;
  h.sender.SendSyn(seconds(0));
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss, h.sent[0].sequence);
  EXPECT(true, h.sent[0].syn);
  EXPECT(false, h.sent[0].retransmission);
  EXPECT(1U, h.sender.BytesInFlight());

  TcpSegmentInfo syn_ack{
      .sequence = 5000, .acknowledgment = kTestIss + 1, .syn = true, .ack = true};
  auto res = h.sender.OnSegment(syn_ack, kLargeWindow, milliseconds(200));
  EXPECT(static_cast<int>(TcpAckResult::kAdvanced), static_cast<int>(res));
  EXPECT(true, h.sender.IsSynAcknowledged());
  EXPECT(0U, h.sender.BytesInFlight());
  // IW = 10 * MSS = 10000.
  EXPECT(10000U, h.sender.Cwnd());
  // First RTT sample of 200 ms recorded -> SRTT = 200 ms.
  ASSERT(true, h.sender.Rto().Srtt().has_value());
  EXPECT(TcpTime(milliseconds(200)).count(), h.sender.Rto().Srtt()->count());
}

TEST(TcpSender_SegmentationByEffectiveMssAndPmtuUpdate) {
  SenderHarness h;
  h.Establish();

  std::string data(2500, 'a');
  size_t written = h.sender.Write(data, seconds(1));
  EXPECT(static_cast<size_t>(2500), written);
  ASSERT(static_cast<size_t>(3), h.sent.size());
  EXPECT(static_cast<size_t>(1000), h.sent[0].payload.size());
  EXPECT(static_cast<size_t>(1000), h.sent[1].payload.size());
  EXPECT(static_cast<size_t>(500), h.sent[2].payload.size());
  EXPECT(kTestIss + 1, h.sent[0].sequence);
  EXPECT(kTestIss + 1001, h.sent[1].sequence);
  EXPECT(kTestIss + 2001, h.sent[2].sequence);

  // Shrink MSS (simulating PMTU drop) and verify subsequent segments use it.
  h.sent.clear();
  h.sender.SetMss(600);
  h.sender.Write(std::string(1300, 'b'), seconds(1));
  ASSERT(static_cast<size_t>(3), h.sent.size());
  EXPECT(static_cast<size_t>(600), h.sent[0].payload.size());
  EXPECT(static_cast<size_t>(600), h.sent[1].payload.size());
  EXPECT(static_cast<size_t>(100), h.sent[2].payload.size());
}

TEST(TcpSender_WindowAndCwndLimitsBytesInFlight) {
  SenderHarness h;
  // Peer advertises a 2500-byte window (smaller than IW = 10000).
  h.Establish(milliseconds(100), 2500);

  h.sender.Write(std::string(5000, 'x'), seconds(1));
  ASSERT(static_cast<size_t>(3), h.sent.size());
  EXPECT(2500U, h.sender.BytesInFlight());
  EXPECT(static_cast<size_t>(500), h.sent[2].payload.size());

  // Cumulative ACK of 2500 bytes opens the flight and transmits the remaining
  // 2500 bytes.
  h.sent.clear();
  TcpSegmentInfo ack{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 2500, .ack = true};
  h.sender.OnSegment(ack, 2500, seconds(2));
  ASSERT(static_cast<size_t>(3), h.sent.size());
  EXPECT(2500U, h.sender.BytesInFlight());
}

TEST(TcpSender_KarnsAlgorithmAndRtoCollapseAndAbortAfterEightTimeouts) {
  SenderHarness h;
  h.Establish(milliseconds(100), kLargeWindow);

  // Send 4 segments (4000 bytes) at t = 1s.
  h.sender.Write(std::string(4000, 'd'), seconds(1));
  ASSERT(static_cast<size_t>(4), h.sent.size());
  h.sent.clear();

  // First RTO at t = 2s: ssthresh = max(4000 / 2, 2000) = 2000, cwnd = 1000,
  // oldest segment retransmitted, RTO backs off from 1s to 2s.
  auto deadline = h.sender.TimerDeadline();
  ASSERT(true, deadline.has_value());
  EXPECT(TcpTime(seconds(2)).count(), deadline->count());

  auto timer_res = h.sender.OnTimer(seconds(2));
  EXPECT(static_cast<int>(TcpTimerResult::kRetransmitted),
         static_cast<int>(timer_res));
  EXPECT(2000U, h.sender.Ssthresh());
  EXPECT(1000U, h.sender.Cwnd());
  EXPECT(TcpTime(seconds(2)).count(), h.sender.Rto().Rto().count());
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss + 1, h.sent[0].sequence);
  EXPECT(true, h.sent[0].retransmission);

  // Karn's algorithm: an ACK for the retransmitted segment must NOT update
  // SRTT (which stays 100 ms from the handshake, not the 1500 ms elapsed).
  TcpSegmentInfo ack1{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 1000, .ack = true};
  h.sender.OnSegment(ack1, kLargeWindow, milliseconds(2500));
  EXPECT(TcpTime(milliseconds(100)).count(), h.sender.Rto().Srtt()->count());

  // Now let 8 consecutive timeouts expire without ACKs and verify abort on the
  // 8th timeout.
  TcpTime now = milliseconds(2500);
  for (int i = 1; i <= 7; i++) {
    auto next_dl = h.sender.TimerDeadline();
    ASSERT(true, next_dl.has_value());
    now = *next_dl;
    EXPECT(static_cast<int>(TcpTimerResult::kRetransmitted),
           static_cast<int>(h.sender.OnTimer(now)));
    EXPECT(false, h.sender.IsAborted());
  }
  auto abort_dl = h.sender.TimerDeadline();
  ASSERT(true, abort_dl.has_value());
  EXPECT(static_cast<int>(TcpTimerResult::kAborted),
         static_cast<int>(h.sender.OnTimer(*abort_dl)));
  EXPECT(true, h.sender.IsAborted());
  EXPECT(false, h.sender.TimerDeadline().has_value());
}

TEST(TcpSender_NewRenoFastRetransmitAndRecovery) {
  SenderHarness h;
  h.Establish(milliseconds(100), kLargeWindow);

  // Send 6 segments (6000 bytes): seq 1001..7001.
  h.sender.Write(std::string(6000, 'p'), seconds(1));
  ASSERT(static_cast<size_t>(6), h.sent.size());
  h.sent.clear();

  // Three duplicate ACKs for 1001 trigger fast retransmit of segment 1001..2001.
  TcpSegmentInfo dup_ack{
      .sequence = 5001, .acknowledgment = kTestIss + 1, .ack = true};
  EXPECT(static_cast<int>(TcpAckResult::kDuplicate),
         static_cast<int>(h.sender.OnSegment(dup_ack, kLargeWindow, seconds(1))));
  EXPECT(true, h.sent.empty());

  EXPECT(static_cast<int>(TcpAckResult::kDuplicate),
         static_cast<int>(h.sender.OnSegment(dup_ack, kLargeWindow, seconds(1))));
  EXPECT(true, h.sent.empty());

  EXPECT(static_cast<int>(TcpAckResult::kDuplicate),
         static_cast<int>(h.sender.OnSegment(dup_ack, kLargeWindow, seconds(1))));
  EXPECT(true, h.sender.InFastRecovery());
  // FlightSize was 6000 -> ssthresh = 3000, cwnd = 3000 + 3000 = 6000.
  EXPECT(3000U, h.sender.Ssthresh());
  EXPECT(6000U, h.sender.Cwnd());
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss + 1, h.sent[0].sequence);
  EXPECT(true, h.sent[0].retransmission);
  h.sent.clear();

  // Partial ACK (acknowledging 1001..3001, still below recover = 7001) stays in
  // recovery and immediately retransmits 3001..4001.
  TcpSegmentInfo partial_ack{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 2000, .ack = true};
  h.sender.OnSegment(partial_ack, kLargeWindow, seconds(1));
  EXPECT(true, h.sender.InFastRecovery());
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss + 2001, h.sent[0].sequence);
  EXPECT(true, h.sent[0].retransmission);
  h.sent.clear();

  // Full ACK reaching recover (7001) exits recovery and deflates cwnd to
  // ssthresh (3000).
  TcpSegmentInfo full_ack{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 6000, .ack = true};
  h.sender.OnSegment(full_ack, kLargeWindow, seconds(1));
  EXPECT(false, h.sender.InFastRecovery());
  EXPECT(3000U, h.sender.Cwnd());
}

TEST(TcpSender_SlowStartAndCongestionAvoidance) {
  SenderHarness h;
  h.Establish(milliseconds(100), kLargeWindow);

  // Trigger an RTO with 4000 bytes in flight so ssthresh = 2000, cwnd = 1000.
  h.sender.Write(std::string(4000, 'c'), seconds(1));
  h.sender.OnTimer(seconds(2));
  EXPECT(1000U, h.sender.Cwnd());
  EXPECT(2000U, h.sender.Ssthresh());

  // ACKing 1000 bytes in slow start grows cwnd from 1000 to 2000 (reaching
  // ssthresh).
  TcpSegmentInfo ack1{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 1000, .ack = true};
  h.sender.OnSegment(ack1, kLargeWindow, seconds(2));
  EXPECT(2000U, h.sender.Cwnd());

  // Now in congestion avoidance (cwnd == ssthresh == 2000): ACKing 2000 bytes
  // grows cwnd by 1 MSS (to 3000).
  TcpSegmentInfo ack2{
      .sequence = 5001, .acknowledgment = kTestIss + 1 + 3000, .ack = true};
  h.sender.OnSegment(ack2, kLargeWindow, seconds(2));
  EXPECT(3000U, h.sender.Cwnd());
}

TEST(TcpSender_ZeroWindowPersistProbeAndRecovery) {
  SenderHarness h;
  // Peer advertises a zero window in the SYN-ACK.
  h.Establish(milliseconds(100), 0);

  h.sender.Write("hello", seconds(1));
  // Nothing sent immediately because peer window is 0.
  EXPECT(true, h.sent.empty());
  EXPECT(0U, h.sender.BytesInFlight());

  // Persist timer is armed for 1s.
  auto dl1 = h.sender.TimerDeadline();
  ASSERT(true, dl1.has_value());
  EXPECT(TcpTime(seconds(2)).count(), dl1->count());

  // When persist timer expires, a 1-byte zero-window probe is transmitted.
  EXPECT(static_cast<int>(TcpTimerResult::kProbeSent),
         static_cast<int>(h.sender.OnTimer(seconds(2))));
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss + 1, h.sent[0].sequence);
  EXPECT(std::string("h"), h.sent[0].payload);
  h.sent.clear();

  // Next persist deadline backs off to 2s.
  auto dl2 = h.sender.TimerDeadline();
  ASSERT(true, dl2.has_value());
  EXPECT(TcpTime(seconds(4)).count(), dl2->count());

  // Peer ACKs the probe byte and opens its window to 4096; remaining "ello" is
  // transmitted immediately.
  TcpSegmentInfo win_open{
      .sequence = 5001, .acknowledgment = kTestIss + 2, .ack = true};
  h.sender.OnSegment(win_open, 4096, seconds(3));
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(kTestIss + 2, h.sent[0].sequence);
  EXPECT(std::string("ello"), h.sent[0].payload);
}

TEST(TcpSender_FinPiggybackAndRetransmission) {
  SenderHarness h;
  h.Establish(milliseconds(100), kLargeWindow);

  h.sender.Write("bye", seconds(1));
  h.sender.Close(seconds(1));
  // "bye" was sent on Write(), and Close() sent the FIN.
  ASSERT(static_cast<size_t>(2), h.sent.size());
  EXPECT(std::string("bye"), h.sent[0].payload);
  EXPECT(false, h.sent[0].fin);
  EXPECT(true, h.sent[1].fin);
  EXPECT(kTestIss + 4, h.sent[1].sequence);
  h.sent.clear();

  // Peer ACKs "bye" (seq 1004), leaving only FIN unacknowledged.
  TcpSegmentInfo ack_data{
      .sequence = 5001, .acknowledgment = kTestIss + 4, .ack = true};
  h.sender.OnSegment(ack_data, kLargeWindow, milliseconds(1100));
  EXPECT(false, h.sender.IsFinAcknowledged());

  // Timeout retransmits the standalone FIN.
  auto fin_dl = h.sender.TimerDeadline();
  ASSERT(true, fin_dl.has_value());
  EXPECT(static_cast<int>(TcpTimerResult::kRetransmitted),
         static_cast<int>(h.sender.OnTimer(*fin_dl)));
  ASSERT(static_cast<size_t>(1), h.sent.size());
  EXPECT(true, h.sent[0].fin);
  EXPECT(true, h.sent[0].retransmission);

  // Peer ACKs the FIN (seq 1005).
  TcpSegmentInfo ack_fin{
      .sequence = 5001, .acknowledgment = kTestIss + 5, .ack = true};
  h.sender.OnSegment(ack_fin, kLargeWindow, *fin_dl + milliseconds(50));
  EXPECT(true, h.sender.IsFinAcknowledged());
  EXPECT(false, h.sender.TimerDeadline().has_value());
}

}  // namespace
