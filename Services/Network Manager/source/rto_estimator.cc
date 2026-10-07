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

#include "rto_estimator.h"

#include <algorithm>

namespace {

// RTO before any RTT sample has been taken (RFC 6298 §2.1).
constexpr std::chrono::microseconds kInitialRto = std::chrono::seconds(1);

// Lower bound on the RTO (RFC 6298 §2.4).
constexpr std::chrono::microseconds kMinimumRto = std::chrono::seconds(1);

// Upper bound on the RTO (RFC 6298 §2.5 allows 60 s or more).
constexpr std::chrono::microseconds kMaximumRto = std::chrono::seconds(60);

// Clock granularity G used in the RTO formula.
constexpr std::chrono::microseconds kClockGranularity =
    std::chrono::milliseconds(1);

// Variance multiplier K (RFC 6298 §2).
constexpr int kVarianceMultiplier = 4;

}  // namespace

RtoLimits RtoLimits::Default() {
  return RtoLimits{.initial = kInitialRto,
                   .minimum = kMinimumRto,
                   .maximum = kMaximumRto,
                   .granularity = kClockGranularity};
}

RtoEstimator::RtoEstimator() : RtoEstimator(RtoLimits::Default()) {}

RtoEstimator::RtoEstimator(const RtoLimits& limits)
    : limits_(limits), rto_(Clamp(limits.initial)) {}

void RtoEstimator::AddSample(std::chrono::microseconds rtt) {
  if (rtt.count() < 0) rtt = std::chrono::microseconds(0);

  if (!srtt_) {
    // First measurement (RFC 6298 §2.2).
    srtt_ = rtt;
    rttvar_ = rtt / 2;
  } else {
    // Subsequent measurements (RFC 6298 §2.3), alpha = 1/8, beta = 1/4.
    std::chrono::microseconds delta = *srtt_ > rtt ? *srtt_ - rtt : rtt - *srtt_;
    rttvar_ = (*rttvar_ * 3 + delta) / 4;
    srtt_ = (*srtt_ * 7 + rtt) / 8;
  }

  rto_ = Clamp(*srtt_ +
               std::max(limits_.granularity, *rttvar_ * kVarianceMultiplier));
}

void RtoEstimator::Backoff() { rto_ = Clamp(rto_ * 2); }

std::chrono::microseconds RtoEstimator::Clamp(
    std::chrono::microseconds rto) const {
  return std::clamp(rto, limits_.minimum, limits_.maximum);
}
