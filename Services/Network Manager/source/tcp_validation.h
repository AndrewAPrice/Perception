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

#include <optional>

#include "types.h"

// The header fields of a received segment needed for validation.
struct TcpSegmentInfo {
  // SEG.SEQ.
  uint32 sequence = 0;
  // SEG.ACK (meaningful only when `ack` is set).
  uint32 acknowledgment = 0;
  // Number of payload bytes.
  uint32 payload_length = 0;
  // SYN flag.
  bool syn = false;
  // ACK flag.
  bool ack = false;
  // FIN flag.
  bool fin = false;
  // RST flag.
  bool rst = false;
};

// Returns SEG.LEN: payload bytes plus one each for SYN and FIN.
uint32 SegmentLength(const TcpSegmentInfo& segment);

// Returns true if a segment is acceptable per the RFC 9293 §3.10.7.4
// sequence check against RCV.NXT and RCV.WND.
bool IsSegmentAcceptable(const TcpSegmentInfo& segment, uint32 rcv_nxt,
                         uint32 rcv_wnd);

// What to do with a received RST in a synchronized state (RFC 5961 §3).
enum class RstAction {
  // The sequence number is outside the window; ignore the segment.
  kDrop,
  // The sequence number is exactly RCV.NXT; reset the connection.
  kReset,
  // The sequence number is in the window but not RCV.NXT; reply with a
  // challenge ACK and keep the connection.
  kChallengeAck,
};

// Classifies a RST received in a synchronized state.
RstAction ValidateRst(uint32 sequence, uint32 rcv_nxt, uint32 rcv_wnd);

// Returns true if a RST received in SYN-SENT is acceptable: it must carry an
// ACK that acknowledges the SYN (ISS < SEG.ACK <= SND.NXT).
bool IsAcceptableRstInSynSent(const TcpSegmentInfo& segment, uint32 iss,
                              uint32 snd_nxt);

// The header fields of a RST to send in reply to a segment.
struct TcpResetReply {
  // Sequence number of the RST.
  uint32 sequence = 0;
  // Acknowledgment number (meaningful only when `ack` is set).
  uint32 acknowledgment = 0;
  // Whether the ACK flag is set.
  bool ack = false;
};

// Builds the RST reply for a segment that matched no connection, per
// RFC 9293 §3.10.7.1. Returns nullopt when the segment itself is a RST, since a
// RST is never sent in response to a RST.
std::optional<TcpResetReply> BuildResetReply(const TcpSegmentInfo& segment);
