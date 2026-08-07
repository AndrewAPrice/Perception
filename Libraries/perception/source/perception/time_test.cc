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

#include "perception/time.h"

#include <string>

#include "testing.h"

TEST(FormatTime) {
  // Negative values
  EXPECT(std::string("-10s"), perception::FormatTime(-10.0));
  EXPECT(std::string("0s"), perception::FormatTime(-0.1));
  EXPECT(std::string("-1m 5s"), perception::FormatTime(-65.0));
  EXPECT(std::string("-1h 1m 5s"), perception::FormatTime(-3665.0));
  EXPECT(std::string("-1d 3h 2m 1s"), perception::FormatTime(-97321.0));

  // Zero seconds
  EXPECT(std::string("0s"), perception::FormatTime(0.0));

  // Fractional seconds (round down / floor)
  EXPECT(std::string("0s"), perception::FormatTime(0.9));
  EXPECT(std::string("45s"), perception::FormatTime(45.8));
  EXPECT(std::string("59s"), perception::FormatTime(59.9));

  // Seconds < 1 min
  EXPECT(std::string("1s"), perception::FormatTime(1.0));
  EXPECT(std::string("45s"), perception::FormatTime(45.0));
  EXPECT(std::string("59s"), perception::FormatTime(59.0));

  // Minutes
  EXPECT(std::string("1m 0s"), perception::FormatTime(60.0));
  EXPECT(std::string("1m 1s"), perception::FormatTime(61.0));
  EXPECT(std::string("2m 5s"), perception::FormatTime(125.0));
  EXPECT(std::string("59m 59s"), perception::FormatTime(3599.0));

  // Hours
  EXPECT(std::string("1h 0m 0s"), perception::FormatTime(3600.0));
  EXPECT(std::string("1h 0m 5s"), perception::FormatTime(3605.0));
  EXPECT(std::string("1h 1m 5s"), perception::FormatTime(3665.0));
  EXPECT(std::string("23h 59m 59s"), perception::FormatTime(86399.0));

  // Days
  EXPECT(std::string("1d 0h 0m 0s"), perception::FormatTime(86400.0));
  EXPECT(std::string("1d 0h 0m 5s"), perception::FormatTime(86405.0));
  EXPECT(std::string("1d 1h 1m 1s"), perception::FormatTime(90061.0));
  EXPECT(std::string("10d 12h 30m 45s"), perception::FormatTime(909045.0));
}
