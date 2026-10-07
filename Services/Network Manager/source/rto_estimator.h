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

#include <chrono>
#include <optional>

// Bounds applied to the retransmission timeout.
struct RtoLimits {
  // RTO used before the first RTT sample (RFC 6298 §2.1).
  std::chrono::microseconds initial;
  // Lower clamp for the RTO (RFC 6298 §2.4).
  std::chrono::microseconds minimum;
  // Upper clamp for the RTO, including after backoff (RFC 6298 §2.5).
  std::chrono::microseconds maximum;
  // Clock granularity G (RFC 6298 §2.3).
  std::chrono::microseconds granularity;

  // Returns the defaults from the design: initial 1 s, min 1 s, max 60 s.
  static RtoLimits Default();
};

// Computes the TCP retransmission timeout per RFC 6298. Callers apply Karn's
// algorithm by only calling AddSample() for segments that were never
// retransmitted.
class RtoEstimator {
 public:
  // Creates an estimator with RtoLimits::Default().
  RtoEstimator();

  // Creates an estimator with custom limits.
  explicit RtoEstimator(const RtoLimits& limits);

  // Folds a round-trip time measurement into SRTT/RTTVAR and recomputes the
  // RTO. This also clears any backoff (RFC 6298 §5.7).
  void AddSample(std::chrono::microseconds rtt);

  // Doubles the RTO up to the maximum after a retransmission timeout
  // (RFC 6298 §5.5).
  void Backoff();

  // Returns the current retransmission timeout.
  std::chrono::microseconds Rto() const { return rto_; }

  // Returns the smoothed RTT, or nullopt before the first sample.
  std::optional<std::chrono::microseconds> Srtt() const { return srtt_; }

  // Returns the RTT variance, or nullopt before the first sample.
  std::optional<std::chrono::microseconds> Rttvar() const { return rttvar_; }

 private:
  // Clamps a raw RTO value to the configured limits.
  std::chrono::microseconds Clamp(std::chrono::microseconds rto) const;

  // Configured bounds.
  RtoLimits limits_;
  // Smoothed round-trip time.
  std::optional<std::chrono::microseconds> srtt_;
  // Round-trip time variation.
  std::optional<std::chrono::microseconds> rttvar_;
  // Current retransmission timeout, including backoff.
  std::chrono::microseconds rto_;
};
