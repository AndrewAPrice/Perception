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

#include <algorithm>
#include <utility>

namespace {

// Default capacity of the TCP send buffer in bytes (256 KiB).
constexpr size_t kDefaultSendBufferCapacity = 256 * 1024;

// Default number of consecutive retransmission timeouts that aborts a
// connection.
constexpr int kDefaultMaxConsecutiveTimeouts = 8;

// Default number of consecutive SYN retransmission timeouts that aborts a
// connection attempt.
constexpr int kDefaultMaxSynTimeouts = 8;

// Initial congestion window in segments (RFC 6928).
constexpr uint32 kInitialWindowSegments = 10;

// Initial slow-start threshold in bytes before any loss occurs.
constexpr uint32 kInitialSsthresh = 1U << 30;

// Maximum congestion window in bytes to guard against 32-bit overflow.
constexpr uint32 kMaxCwnd = 1U << 30;

// Duplicate ACK threshold for triggering NewReno fast retransmit (RFC 5681).
constexpr int kFastRetransmitThreshold = 3;

// Minimum slow-start threshold in segments after congestion (RFC 5681 eq. 4).
constexpr uint32 kMinSsthreshSegments = 2;

// Minimum consumed bytes at the front of the send buffer before erasing them.
constexpr size_t kBufferCompactThreshold = 4096;

// Maximum shift exponent applied when backing off the persist timer.
constexpr int kMaxPersistBackoffShift = 6;

}  // namespace

TcpSenderOptions TcpSenderOptions::Default(uint16 mss) {
  return TcpSenderOptions{
      .mss = mss,
      .send_buffer_capacity = kDefaultSendBufferCapacity,
      .max_consecutive_timeouts = kDefaultMaxConsecutiveTimeouts,
      .max_syn_timeouts = kDefaultMaxSynTimeouts,
      .rto_limits = RtoLimits::Default()};
}

TcpSender::TcpSender(uint32 iss, const TcpSenderOptions& options,
                     TransmitFunction transmit)
    : options_(options),
      transmit_(std::move(transmit)),
      rto_(options.rto_limits),
      iss_(iss),
      snd_una_(iss),
      snd_nxt_(iss),
      snd_max_(iss),
      mss_(std::max<uint16>(1, options.mss)),
      buffer_seq_(iss + 1),
      cwnd_(InitialWindow()),
      ssthresh_(kInitialSsthresh),
      recover_(iss) {}

void TcpSender::SendSyn(TcpTime now) {
  if (aborted_) return;
  EmitSegment(iss_, 0, now, EmitMode::kNormal);
  UpdateTimer(now, true);
}

size_t TcpSender::Write(std::string_view data, TcpTime now) {
  if (fin_queued_ || aborted_ || data.empty()) return 0;

  size_t writable = std::min(data.size(), FreeBufferSpace());
  if (writable == 0) return 0;

  buffer_.append(data.data(), writable);
  TrySend(now);
  UpdateTimer(now, false);
  return writable;
}

void TcpSender::Close(TcpTime now) {
  if (fin_queued_ || aborted_) return;
  fin_queued_ = true;
  TrySend(now);
  UpdateTimer(now, false);
}

TcpAckResult TcpSender::OnSegment(const TcpSegmentInfo& segment, uint32 window,
                                  TcpTime now) {
  if (aborted_) return TcpAckResult::kNoProgress;
  if (!segment.ack) return TcpAckResult::kNoAck;
  if (SeqGreater(segment.acknowledgment, snd_max_))
    return TcpAckResult::kUnsentData;
  if (SeqLess(segment.acknowledgment, snd_una_)) return TcpAckResult::kOld;

  if (!syn_acked_) {
    if (segment.acknowledgment == snd_una_) {
      UpdateWindow(segment, window);
      return TcpAckResult::kNoProgress;
    }
    UpdateWindow(segment, window);
    OnNewAck(segment.acknowledgment, now);
    return TcpAckResult::kAdvanced;
  }

  if (segment.acknowledgment == snd_una_) {
    bool is_duplicate_ack = BytesInFlight() > 0 &&
                            segment.payload_length == 0 && !segment.syn &&
                            !segment.fin && window_initialized_ &&
                            window == snd_wnd_;
    UpdateWindow(segment, window);
    if (is_duplicate_ack) {
      OnDuplicateAck(now);
      return TcpAckResult::kDuplicate;
    }
    TrySend(now);
    UpdateTimer(now, false);
    return TcpAckResult::kNoProgress;
  }

  UpdateWindow(segment, window);
  OnNewAck(segment.acknowledgment, now);
  return TcpAckResult::kAdvanced;
}

void TcpSender::SetMss(uint16 mss) {
  mss_ = std::max<uint16>(1, mss);
  if (SeqLessOrEqual(snd_max_, iss_ + 1) && !in_recovery_ &&
      ssthresh_ == kInitialSsthresh) {
    cwnd_ = syn_retransmitted_ ? static_cast<uint32>(mss_) : InitialWindow();
  } else {
    cwnd_ = std::max(cwnd_, static_cast<uint32>(mss_));
  }
}

std::optional<TcpTime> TcpSender::TimerDeadline() const {
  if (timer_kind_ == TimerKind::kNone || aborted_) return std::nullopt;
  return timer_deadline_;
}

TcpTimerResult TcpSender::OnTimer(TcpTime now) {
  if (timer_kind_ == TimerKind::kNone || aborted_ || now < timer_deadline_)
    return TcpTimerResult::kNone;

  if (timer_kind_ == TimerKind::kRetransmission)
    return OnRetransmissionTimeout(now);

  // Persist timer expired: send a 1-byte zero-window probe.
  EmitSegment(snd_nxt_, 1, now, EmitMode::kProbe);
  if (persist_backoff_ < kMaxPersistBackoffShift) persist_backoff_++;
  timer_deadline_ = now + PersistInterval();
  return TcpTimerResult::kProbeSent;
}

size_t TcpSender::FreeBufferSpace() const {
  if (fin_queued_ || aborted_) return 0;
  size_t buffered = BufferedBytes();
  if (buffered >= options_.send_buffer_capacity) return 0;
  return options_.send_buffer_capacity - buffered;
}

uint32 TcpSender::EmitSegment(uint32 sequence, uint32 max_payload, TcpTime now,
                              EmitMode mode) {
  TcpOutgoingSegment segment;
  segment.sequence = sequence;

  if (sequence == iss_) {
    segment.syn = true;
  } else {
    uint32 data_end = DataEndSeq();
    if (SeqLess(sequence, data_end)) {
      uint32 available = data_end - sequence;
      uint32 take =
          std::min({available, max_payload, static_cast<uint32>(mss_)});
      size_t offset =
          buffer_head_ + static_cast<size_t>(sequence - buffer_seq_);
      segment.payload = std::string_view(buffer_).substr(offset, take);
    }
    if (fin_queued_ &&
        sequence + static_cast<uint32>(segment.payload.size()) == data_end &&
        (mode == EmitMode::kNormal || segment.payload.empty())) {
      segment.fin = true;
    }
  }

  uint32 seg_len = static_cast<uint32>(segment.payload.size()) +
                   (segment.syn ? 1U : 0U) + (segment.fin ? 1U : 0U);
  if (seg_len == 0) return 0;

  uint32 end = sequence + seg_len;
  segment.retransmission = SeqLess(sequence, snd_max_);

  if (mode == EmitMode::kNormal && SeqLess(snd_nxt_, end)) snd_nxt_ = end;
  if (SeqLess(snd_max_, end)) snd_max_ = end;

  if (mode == EmitMode::kNormal && !segment.retransmission && !timing_) {
    timing_ = true;
    timed_end_seq_ = end;
    timed_start_ = now;
  }

  transmit_(segment);
  return seg_len;
}

void TcpSender::RetransmitOldest(TcpTime now) {
  timing_ = false;
  EmitSegment(snd_una_, mss_, now, EmitMode::kNormal);
}

void TcpSender::TrySend(TcpTime now) {
  while (syn_acked_ && !aborted_) {
    uint32 effective_window =
        window_initialized_ ? std::min(cwnd_, snd_wnd_) : cwnd_;
    uint32 data_end = DataEndSeq();
    uint32 stream_end = fin_queued_ ? data_end + 1 : data_end;
    if (SeqGreaterOrEqual(snd_nxt_, stream_end)) break;

    uint32 flight = BytesInFlight();
    uint32 unsent_data =
        SeqLess(snd_nxt_, data_end) ? (data_end - snd_nxt_) : 0;

    if (unsent_data > 0) {
      if (flight >= effective_window) break;
      uint32 available_window = effective_window - flight;
      uint32 max_payload = std::min(unsent_data, available_window);
      if (EmitSegment(snd_nxt_, max_payload, now, EmitMode::kNormal) == 0)
        break;
    } else {
      // Only the FIN remains unsent.
      if (flight >= effective_window && flight > 0) break;
      if (EmitSegment(snd_nxt_, 0, now, EmitMode::kNormal) == 0) break;
    }
  }
}

void TcpSender::OnNewAck(uint32 ack, TcpTime now) {
  bool was_syn_acked = syn_acked_;
  uint32 acked_seq = ack - snd_una_;
  uint32 data_acked = acked_seq;

  if (!syn_acked_ && SeqGreater(ack, iss_)) {
    syn_acked_ = true;
    cwnd_ = syn_retransmitted_ ? static_cast<uint32>(mss_) : InitialWindow();
    data_acked = acked_seq - 1;
  }

  uint32 data_end = DataEndSeq();
  if (fin_queued_ && SeqGreater(ack, data_end)) {
    fin_acked_ = true;
    if (data_acked > 0) data_acked--;
  }

  uint32 trim_end = SeqGreater(ack, data_end) ? data_end : ack;
  if (SeqGreater(trim_end, buffer_seq_)) {
    uint32 trim = trim_end - buffer_seq_;
    buffer_head_ += trim;
    buffer_seq_ += trim;
    if (buffer_head_ == buffer_.size()) {
      buffer_.clear();
      buffer_head_ = 0;
    } else if (buffer_head_ >= kBufferCompactThreshold) {
      buffer_.erase(0, buffer_head_);
      buffer_head_ = 0;
    }
  }

  snd_una_ = ack;
  if (SeqLess(snd_nxt_, snd_una_)) snd_nxt_ = snd_una_;

  if (timing_ && SeqGreaterOrEqual(ack, timed_end_seq_)) {
    timing_ = false;
    rto_.AddSample(now - timed_start_);
  }

  consecutive_timeouts_ = 0;
  duplicate_acks_ = 0;

  if (in_recovery_) {
    if (SeqGreaterOrEqual(ack, recover_)) {
      cwnd_ = ssthresh_;
      in_recovery_ = false;
      bytes_acked_in_avoidance_ = 0;
    } else {
      RetransmitOldest(now);
      if (cwnd_ > data_acked) {
        cwnd_ -= data_acked;
      } else {
        cwnd_ = 0;
      }
      if (data_acked >= mss_) cwnd_ += mss_;
      cwnd_ = std::max(cwnd_, static_cast<uint32>(mss_));
    }
  } else if (was_syn_acked && data_acked > 0) {
    uint32 remaining = data_acked;
    if (cwnd_ < ssthresh_) {
      uint32 room = ssthresh_ - cwnd_;
      uint32 slow_start_increase = std::min(remaining, room);
      cwnd_ = std::min(kMaxCwnd, cwnd_ + slow_start_increase);
      remaining -= slow_start_increase;
    }
    if (remaining > 0 && cwnd_ >= ssthresh_) {
      bytes_acked_in_avoidance_ += remaining;
      while (bytes_acked_in_avoidance_ >= cwnd_) {
        bytes_acked_in_avoidance_ -= cwnd_;
        cwnd_ = std::min(kMaxCwnd, cwnd_ + static_cast<uint32>(mss_));
      }
    }
  }

  if (timer_kind_ == TimerKind::kPersist) timer_kind_ = TimerKind::kNone;
  TrySend(now);
  UpdateTimer(now, true);
}

void TcpSender::OnDuplicateAck(TcpTime now) {
  duplicate_acks_++;
  if (in_recovery_) {
    cwnd_ = std::min(kMaxCwnd, cwnd_ + static_cast<uint32>(mss_));
    TrySend(now);
    UpdateTimer(now, false);
    return;
  }

  if (duplicate_acks_ >= kFastRetransmitThreshold) {
    ssthresh_ = ReducedSsthresh();
    recover_ = snd_max_;
    in_recovery_ = true;
    bytes_acked_in_avoidance_ = 0;
    RetransmitOldest(now);
    cwnd_ = ssthresh_ + kFastRetransmitThreshold * static_cast<uint32>(mss_);
    TrySend(now);
    UpdateTimer(now, true);
  }
}

TcpTimerResult TcpSender::OnRetransmissionTimeout(TcpTime now) {
  consecutive_timeouts_++;
  int max_timeouts = syn_acked_ ? options_.max_consecutive_timeouts
                                : options_.max_syn_timeouts;
  if (consecutive_timeouts_ >= max_timeouts) {
    aborted_ = true;
    timer_kind_ = TimerKind::kNone;
    return TcpTimerResult::kAborted;
  }

  if (syn_acked_) {
    if (consecutive_timeouts_ == 1) ssthresh_ = ReducedSsthresh();
    cwnd_ = static_cast<uint32>(mss_);
    bytes_acked_in_avoidance_ = 0;
    duplicate_acks_ = 0;
    in_recovery_ = false;
    recover_ = snd_max_;
  } else {
    syn_retransmitted_ = true;
    cwnd_ = static_cast<uint32>(mss_);
  }

  snd_nxt_ = snd_una_;
  RetransmitOldest(now);
  rto_.Backoff();
  UpdateTimer(now, true);
  return TcpTimerResult::kRetransmitted;
}

void TcpSender::UpdateWindow(const TcpSegmentInfo& segment, uint32 window) {
  if (!window_initialized_ || SeqLess(snd_wl1_, segment.sequence) ||
      (snd_wl1_ == segment.sequence &&
       SeqLessOrEqual(snd_wl2_, segment.acknowledgment))) {
    window_initialized_ = true;
    snd_wnd_ = window;
    snd_wl1_ = segment.sequence;
    snd_wl2_ = segment.acknowledgment;
    if (snd_wnd_ > 0) persist_backoff_ = 0;
  }
}

void TcpSender::UpdateTimer(TcpTime now, bool restart_retransmission) {
  if (aborted_) {
    timer_kind_ = TimerKind::kNone;
    return;
  }

  if (BytesInFlight() > 0) {
    if (timer_kind_ != TimerKind::kRetransmission || restart_retransmission) {
      timer_kind_ = TimerKind::kRetransmission;
      timer_deadline_ = now + rto_.Rto();
    }
    return;
  }

  if (syn_acked_ && window_initialized_ && snd_wnd_ == 0 && HasUnsentData()) {
    if (timer_kind_ != TimerKind::kPersist) {
      timer_kind_ = TimerKind::kPersist;
      timer_deadline_ = now + PersistInterval();
    }
    return;
  }

  timer_kind_ = TimerKind::kNone;
}

uint32 TcpSender::DataEndSeq() const {
  return buffer_seq_ + static_cast<uint32>(BufferedBytes());
}

bool TcpSender::HasUnsentData() const {
  return SeqLess(snd_nxt_, DataEndSeq());
}

uint32 TcpSender::InitialWindow() const {
  return kInitialWindowSegments * static_cast<uint32>(mss_);
}

TcpTime TcpSender::PersistInterval() const {
  int shift = std::min(persist_backoff_, kMaxPersistBackoffShift);
  TcpTime scaled = rto_.Rto() * (1LL << shift);
  return std::clamp(scaled, options_.rto_limits.minimum,
                    options_.rto_limits.maximum);
}

uint32 TcpSender::ReducedSsthresh() const {
  return std::max(BytesInFlight() / 2,
                  kMinSsthreshSegments * static_cast<uint32>(mss_));
}
