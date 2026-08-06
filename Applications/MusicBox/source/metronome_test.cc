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

#include "metronome.h"

#include "testing.h"

namespace {

TEST(Metronome_IsMajorTick_4_4_Time) {
  int beats_per_bar = 4;
  EXPECT(true, Metronome::IsMajorTick(0, beats_per_bar));   // Bar 0, Beat 0 -> Major
  EXPECT(false, Metronome::IsMajorTick(1, beats_per_bar));  // Bar 0, Beat 1 -> Minor
  EXPECT(false, Metronome::IsMajorTick(2, beats_per_bar));  // Bar 0, Beat 2 -> Minor
  EXPECT(false, Metronome::IsMajorTick(3, beats_per_bar));  // Bar 0, Beat 3 -> Minor
  EXPECT(true, Metronome::IsMajorTick(4, beats_per_bar));   // Bar 1, Beat 0 -> Major
  EXPECT(false, Metronome::IsMajorTick(5, beats_per_bar));  // Bar 1, Beat 1 -> Minor
}

TEST(Metronome_IsMajorTick_3_4_Time) {
  int beats_per_bar = 3;
  EXPECT(true, Metronome::IsMajorTick(0, beats_per_bar));   // Bar 0, Beat 0 -> Major
  EXPECT(false, Metronome::IsMajorTick(1, beats_per_bar));  // Bar 0, Beat 1 -> Minor
  EXPECT(false, Metronome::IsMajorTick(2, beats_per_bar));  // Bar 0, Beat 2 -> Minor
  EXPECT(true, Metronome::IsMajorTick(3, beats_per_bar));   // Bar 1, Beat 0 -> Major
}

TEST(Metronome_ToggleAndReset) {
  Metronome metronome;
  EXPECT(false, metronome.IsEnabled());

  metronome.SetEnabled(true);
  EXPECT(true, metronome.IsEnabled());

  metronome.SetEnabled(false);
  EXPECT(false, metronome.IsEnabled());
}

}  // namespace
