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

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "rto_estimator.h"
#include "tcp_sequence.h"
#include "tcp_validation.h"
#include "types.h"

// A segment that the sender asks to be transmitted. The caller builds the TCP
// header from these fields plus its receive state (ACK flag and number, and the
// advertised window).
struct TcpOutgoingSegment {
  // Sequence number of the first byte (or of the SYN/FIN).
  uint32 sequence = 0;
  // Payload bytes. Only valid for the duration of the transmit callback.
  std::string_view payload;
  // Whether the SYN flag is set (the payload is then empty).
  bool syn = false;
  // Whether the FIN flag is set.
  bool fin = false;
  // Whether any part of this segment was transmitted before.
  bool retransmission = false;
};

// Tunables of a TcpSender.
struct TcpSenderOptions {
  // Initial effective send MSS. Update it with SetMss() once the peer's MSS
  // option and the path MTU are known.
  uint16 mss = 0;
  // Maximum number of unacknowledged plus unsent bytes buffered.
  size_t send_buffer_capacity = 0;
  // Retransmission timeouts in a row, after the handshake, that abort the
  // connection.
  int max_consecutive_timeouts = 0;
  // Retransmission timeouts in a row of the SYN that abort the connection.
  int max_syn_timeouts = 0;
  // Bounds on the retransmission timeout.
  RtoLimits rto_limits;

  // Returns the design defaults for the given initial MSS: 256 KiB buffer,
  // abort after 8 consecutive timeouts, RFC 6298 RTO limits.
  static TcpSenderOptions Default(uint16 mss);
};

// The outcome of processing the ACK fields of a received segment.
enum class TcpAckResult {
  // The segment carried no ACK flag.
  kNoAck,
  // SEG.ACK acknowledges data that was never sent. RFC 9293 requires replying
  // with an ACK and dropping the segment.
  kUnsentData,
  // SEG.ACK is older than SND.UNA and was ignored.
  kOld,
  // SEG.ACK equals SND.UNA but is not a duplicate ACK (for example it carries
  // data or a window update).
  kNoProgress,
  // A duplicate ACK as defined by RFC 5681 §2.
  kDuplicate,
  // SEG.ACK acknowledged new sequence space.
  kAdvanced,
};

// The outcome of OnTimer().
enum class TcpTimerResult {
  // No timer expired.
  kNone,
  // The retransmission timer expired and the oldest segment was resent.
  kRetransmitted,
  // The persist timer expired and a zero-window probe was sent.
  kProbeSent,
  // Too many consecutive timeouts; the caller must reset the connection.
  kAborted,
};

// The send half of a TCP connection: send buffer, segmentation at the
// effective MSS, flow control by the peer window, the retransmission queue and
// timer (RFC 6298 with Karn's algorithm), NewReno congestion control
// (RFC 5681/6582, IW per RFC 6928), and the persist timer.
//
// The retransmission queue is the part of the send buffer between SND.UNA and
// SND.MAX; segments are cut from it again when they are retransmitted. The
// class performs no I/O: segments go to an injected transmit function, and the
// caller supplies the current time and drives OnTimer() at TimerDeadline().
class TcpSender {
 public:
  // Receives each segment to transmit.
  using TransmitFunction = std::function<void(const TcpOutgoingSegment&)>;

  // Creates a sender whose SYN uses sequence number `iss`.
  TcpSender(uint32 iss, const TcpSenderOptions& options,
            TransmitFunction transmit);

  // Transmits the SYN (as the SYN of an active open, or the SYN-ACK of a
  // passive open) and starts the retransmission timer.
  void SendSyn(TcpTime now);

  // Appends as much of `data` as fits in the send buffer and transmits what
  // the windows allow. Returns the number of bytes accepted. Returns 0 after
  // Close().
  size_t Write(std::string_view data, TcpTime now);

  // Queues a FIN after all buffered data.
  void Close(TcpTime now);

  // Processes the ACK number and window of a received segment that already
  // passed the sequence acceptability check. `window` is the peer's
  // advertised window in bytes.
  TcpAckResult OnSegment(const TcpSegmentInfo& segment, uint32 window,
                         TcpTime now);

  // Changes the effective send MSS (peer MSS option or a path MTU change).
  void SetMss(uint16 mss);

  // Returns when OnTimer() next needs to run, or nullopt if no timer is armed.
  std::optional<TcpTime> TimerDeadline() const;

  // Handles an expired retransmission or persist timer. Does nothing if the
  // deadline hasn't been reached.
  TcpTimerResult OnTimer(TcpTime now);

  // Returns the initial send sequence number.
  uint32 Iss() const { return iss_; }

  // Returns SND.UNA, the oldest unacknowledged sequence number.
  uint32 SndUna() const { return snd_una_; }

  // Returns SND.NXT, the next sequence number to send.
  uint32 SndNxt() const { return snd_nxt_; }

  // Returns the highest sequence number sent so far, plus one.
  uint32 SndMax() const { return snd_max_; }

  // Returns SND.WND, the peer's advertised window.
  uint32 SndWnd() const { return snd_wnd_; }

  // Returns the congestion window in bytes.
  uint32 Cwnd() const { return cwnd_; }

  // Returns the slow start threshold in bytes.
  uint32 Ssthresh() const { return ssthresh_; }

  // Returns the effective send MSS.
  uint16 Mss() const { return mss_; }

  // Returns the bytes sent but not yet acknowledged.
  uint32 BytesInFlight() const { return snd_nxt_ - snd_una_; }

  // Returns the bytes held in the send buffer (unacknowledged plus unsent).
  size_t BufferedBytes() const { return buffer_.size() - buffer_head_; }

  // Returns how many more bytes Write() can accept.
  size_t FreeBufferSpace() const;

  // Returns the RTO estimator.
  const RtoEstimator& Rto() const { return rto_; }

  // Returns true once the peer acknowledged the SYN.
  bool IsSynAcknowledged() const { return syn_acked_; }

  // Returns true once the peer acknowledged the FIN.
  bool IsFinAcknowledged() const { return fin_acked_; }

  // Returns true while in NewReno fast recovery.
  bool InFastRecovery() const { return in_recovery_; }

  // Returns true after the connection was aborted by repeated timeouts.
  bool IsAborted() const { return aborted_; }

 private:
  // Whether a segment is a zero-window probe.
  enum class EmitMode { kNormal, kProbe };

  // The timer currently armed.
  enum class TimerKind { kNone, kRetransmission, kPersist };

  // Builds and transmits the segment starting at `sequence` carrying at most
  // `max_payload` bytes. Returns the sequence space consumed.
  uint32 EmitSegment(uint32 sequence, uint32 max_payload, TcpTime now,
                     EmitMode mode);

  // Retransmits the oldest unacknowledged segment.
  void RetransmitOldest(TcpTime now);

  // Transmits new data while the windows allow.
  void TrySend(TcpTime now);

  // Handles an ACK that advanced SND.UNA.
  void OnNewAck(uint32 ack, TcpTime now);

  // Handles a duplicate ACK.
  void OnDuplicateAck(TcpTime now);

  // Handles an expired retransmission timer.
  TcpTimerResult OnRetransmissionTimeout(TcpTime now);

  // Updates SND.WND per RFC 9293 §3.10.7.4 (SND.WL1/SND.WL2 rules).
  void UpdateWindow(const TcpSegmentInfo& segment, uint32 window);

  // Arms, re-arms or stops the timers to match the current state.
  // `restart_retransmission` restarts a running retransmission timer.
  void UpdateTimer(TcpTime now, bool restart_retransmission);

  // Returns the sequence number after the last buffered data byte.
  uint32 DataEndSeq() const;

  // Returns true if there's buffered data that was never sent.
  bool HasUnsentData() const;

  // Returns the RFC 6928 initial window for the current MSS.
  uint32 InitialWindow() const;

  // Returns the current persist timer interval.
  TcpTime PersistInterval() const;

  // Returns max(FlightSize / 2, 2 * MSS) (RFC 5681 equation 4).
  uint32 ReducedSsthresh() const;

  // Configuration.
  TcpSenderOptions options_;
  // Receives segments to transmit.
  TransmitFunction transmit_;
  // RTO estimator.
  RtoEstimator rto_;

  // Initial send sequence number.
  uint32 iss_;
  // Oldest unacknowledged sequence number.
  uint32 snd_una_;
  // Next sequence number to send.
  uint32 snd_nxt_;
  // One past the highest sequence number ever sent.
  uint32 snd_max_;
  // Peer's advertised window.
  uint32 snd_wnd_ = 0;
  // Segment sequence number used for the last window update.
  uint32 snd_wl1_ = 0;
  // Segment acknowledgment number used for the last window update.
  uint32 snd_wl2_ = 0;
  // Whether a window has been received from the peer.
  bool window_initialized_ = false;
  // Effective send MSS.
  uint16 mss_;

  // Buffered bytes; the data starts at buffer_head_.
  std::string buffer_;
  // Offset of the first live byte in buffer_.
  size_t buffer_head_ = 0;
  // Sequence number of the first live byte in buffer_.
  uint32 buffer_seq_;

  // Whether the SYN was acknowledged.
  bool syn_acked_ = false;
  // Whether the SYN had to be retransmitted (initial window becomes 1 MSS).
  bool syn_retransmitted_ = false;
  // Whether Close() queued a FIN.
  bool fin_queued_ = false;
  // Whether the FIN was acknowledged.
  bool fin_acked_ = false;
  // Whether the connection was aborted.
  bool aborted_ = false;

  // Congestion window.
  uint32 cwnd_;
  // Slow start threshold.
  uint32 ssthresh_;
  // Bytes acknowledged during congestion avoidance since cwnd last grew.
  uint32 bytes_acked_in_avoidance_ = 0;
  // Consecutive duplicate ACKs.
  int duplicate_acks_ = 0;
  // Whether NewReno fast recovery is active.
  bool in_recovery_ = false;
  // SND.MAX when recovery (or the last RTO) started (RFC 6582 "recover").
  uint32 recover_;

  // Whether a segment is being timed for an RTT sample.
  bool timing_ = false;
  // ACK number that completes the timed segment.
  uint32 timed_end_seq_ = 0;
  // When the timed segment was sent.
  TcpTime timed_start_{};

  // The armed timer.
  TimerKind timer_kind_ = TimerKind::kNone;
  // When the armed timer expires.
  TcpTime timer_deadline_{};
  // Consecutive retransmission timeouts without progress.
  int consecutive_timeouts_ = 0;
  // Number of zero-window probes sent since the window closed.
  int persist_backoff_ = 0;
};
