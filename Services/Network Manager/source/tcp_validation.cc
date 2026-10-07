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

#include "tcp_validation.h"

#include "tcp_sequence.h"

namespace {

// Returns true if `sequence` lies in [start, start + length).
bool InWindow(uint32 sequence, uint32 start, uint32 length) {
  return SeqGreaterOrEqual(sequence, start) &&
         SeqLess(sequence, start + length);
}

}  // namespace

uint32 SegmentLength(const TcpSegmentInfo& segment) {
  return segment.payload_length + (segment.syn ? 1 : 0) + (segment.fin ? 1 : 0);
}

bool IsSegmentAcceptable(const TcpSegmentInfo& segment, uint32 rcv_nxt,
                         uint32 rcv_wnd) {
  uint32 length = SegmentLength(segment);
  if (length == 0) {
    if (rcv_wnd == 0) return segment.sequence == rcv_nxt;
    return InWindow(segment.sequence, rcv_nxt, rcv_wnd);
  }
  if (rcv_wnd == 0) return false;
  return InWindow(segment.sequence, rcv_nxt, rcv_wnd) ||
         InWindow(segment.sequence + length - 1, rcv_nxt, rcv_wnd);
}

RstAction ValidateRst(uint32 sequence, uint32 rcv_nxt, uint32 rcv_wnd) {
  if (sequence == rcv_nxt) return RstAction::kReset;
  if (InWindow(sequence, rcv_nxt, rcv_wnd)) return RstAction::kChallengeAck;
  return RstAction::kDrop;
}

bool IsAcceptableRstInSynSent(const TcpSegmentInfo& segment, uint32 iss,
                              uint32 snd_nxt) {
  return segment.ack && SeqGreater(segment.acknowledgment, iss) &&
         SeqLessOrEqual(segment.acknowledgment, snd_nxt);
}

std::optional<TcpResetReply> BuildResetReply(const TcpSegmentInfo& segment) {
  if (segment.rst) return std::nullopt;
  if (segment.ack) return TcpResetReply{.sequence = segment.acknowledgment};
  return TcpResetReply{.sequence = 0,
                       .acknowledgment =
                           segment.sequence + SegmentLength(segment),
                       .ack = true};
}
