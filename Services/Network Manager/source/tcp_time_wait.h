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

#include "tcp_sequence.h"

// Tracks the 2 x MSL TIME_WAIT period of a closed connection so late segments
// aren't delivered to a new connection reusing the same 4-tuple.
class TcpTimeWait {
 public:
  // Enters TIME_WAIT at `now`.
  explicit TcpTimeWait(TcpTime now);

  // Handles a segment received in TIME_WAIT. A retransmitted FIN restarts the
  // 2 x MSL timer (RFC 9293 §3.10.7.4). Returns true if the segment must be
  // acknowledged (any FIN).
  bool OnSegment(bool fin, TcpTime now);

  // Returns true once the TIME_WAIT period has elapsed.
  bool IsExpired(TcpTime now) const { return now >= deadline_; }

  // Returns when the TIME_WAIT period ends.
  TcpTime Deadline() const { return deadline_; }

  // Returns the TIME_WAIT duration (2 x MSL = 60 s).
  static TcpTime Duration();

 private:
  // When TIME_WAIT ends.
  TcpTime deadline_;
};
