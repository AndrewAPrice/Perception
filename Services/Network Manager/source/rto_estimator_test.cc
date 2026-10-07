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

#include <chrono>

#include "testing.h"

namespace {

using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::seconds;

// Initial RTO required by RFC 6298 §2.1.
constexpr microseconds kExpectedInitialRto = seconds(1);

// Minimum RTO required by RFC 6298 §2.4.
constexpr microseconds kExpectedMinRto = seconds(1);

// Maximum RTO cap per RFC 6298 §2.5.
constexpr microseconds kExpectedMaxRto = seconds(60);

// Sample RTT used for the first-measurement formula check (1.6 s).
constexpr microseconds kFirstSampleRtt = milliseconds(1600);

// Second sample RTT used for the EWMA update check (0.8 s).
constexpr microseconds kSecondSampleRtt = milliseconds(800);

// Fast LAN RTT sample that clamps to the 1 s minimum RTO.
constexpr microseconds kFastLanRtt = milliseconds(20);

TEST(RtoEstimator_InitialState) {
  RtoEstimator estimator;
  EXPECT(kExpectedInitialRto.count(), estimator.Rto().count());
  EXPECT(false, estimator.Srtt().has_value());
  EXPECT(false, estimator.Rttvar().has_value());
}

TEST(RtoEstimator_FirstSampleFormula) {
  RtoEstimator estimator;
  estimator.AddSample(kFirstSampleRtt);

  ASSERT(true, estimator.Srtt().has_value());
  ASSERT(true, estimator.Rttvar().has_value());
  // SRTT = R = 1600 ms, RTTVAR = R / 2 = 800 ms, RTO = 1600 + 4 * 800 = 4800 ms.
  EXPECT(microseconds(milliseconds(1600)).count(), estimator.Srtt()->count());
  EXPECT(microseconds(milliseconds(800)).count(), estimator.Rttvar()->count());
  EXPECT(microseconds(milliseconds(4800)).count(), estimator.Rto().count());
}

TEST(RtoEstimator_SubsequentSampleEwma) {
  RtoEstimator estimator;
  estimator.AddSample(kFirstSampleRtt);
  estimator.AddSample(kSecondSampleRtt);

  // |SRTT - R'| = |1600 - 800| = 800 ms.
  // RTTVAR = (3/4) * 800 + (1/4) * 800 = 800 ms.
  // SRTT = (7/8) * 1600 + (1/8) * 800 = 1500 ms.
  // RTO = 1500 + 4 * 800 = 4700 ms.
  EXPECT(microseconds(milliseconds(1500)).count(), estimator.Srtt()->count());
  EXPECT(microseconds(milliseconds(800)).count(), estimator.Rttvar()->count());
  EXPECT(microseconds(milliseconds(4700)).count(), estimator.Rto().count());
}

TEST(RtoEstimator_ClampsToMinimumOneSecond) {
  RtoEstimator estimator;
  estimator.AddSample(kFastLanRtt);

  // Raw RTO = 20 + 4 * 10 = 60 ms, clamped to 1000 ms.
  EXPECT(microseconds(milliseconds(20)).count(), estimator.Srtt()->count());
  EXPECT(microseconds(milliseconds(10)).count(), estimator.Rttvar()->count());
  EXPECT(kExpectedMinRto.count(), estimator.Rto().count());
}

TEST(RtoEstimator_ExponentialBackoffAndCapAtSixtySeconds) {
  RtoEstimator estimator;
  EXPECT(microseconds(seconds(1)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(microseconds(seconds(2)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(microseconds(seconds(4)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(microseconds(seconds(8)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(microseconds(seconds(16)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(microseconds(seconds(32)).count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(kExpectedMaxRto.count(), estimator.Rto().count());

  estimator.Backoff();
  EXPECT(kExpectedMaxRto.count(), estimator.Rto().count());

  // A fresh RTT sample clears the backoff (RFC 6298 §5.7).
  estimator.AddSample(kFastLanRtt);
  EXPECT(kExpectedMinRto.count(), estimator.Rto().count());
}

}  // namespace
