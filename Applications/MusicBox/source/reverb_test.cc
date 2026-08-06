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

#include "reverb.h"

#include <vector>

#include "testing.h"

namespace {

TEST(Reverb_BypassWhenDisabled) {
  Reverb reverb;
  reverb.SetEnabled(false);

  std::vector<int16_t> buffer = {1000, 2000, -3000, 4000};
  auto copy = buffer;

  reverb.Process(buffer.data(), buffer.size());
  EXPECT(copy.size(), buffer.size());
  for (size_t i = 0; i < buffer.size(); ++i) {
    EXPECT(copy[i], buffer[i]);
  }
}

TEST(Reverb_ImpulseDecayTail) {
  Reverb reverb;
  reverb.SetEnabled(true);
  reverb.SetMix(0.5f);
  reverb.SetRoomSize(0.8f);

  std::vector<int16_t> buffer(3000, 0);
  buffer[0] = 30000;  // Impulse sample

  reverb.Process(buffer.data(), buffer.size());

  // Verify that subsequent samples contain reverberation tail
  bool has_tail = false;
  for (size_t i = 100; i < buffer.size(); ++i) {
    if (buffer[i] != 0) {
      has_tail = true;
      break;
    }
  }
  EXPECT(true, has_tail);
}

TEST(Reverb_ParameterBounds) {
  Reverb reverb;

  reverb.SetMix(1.5f);
  EXPECT(1.0f, reverb.GetMix());

  reverb.SetMix(-0.5f);
  EXPECT(0.0f, reverb.GetMix());

  reverb.SetRoomSize(2.0f);
  EXPECT(1.0f, reverb.GetRoomSize());

  reverb.SetDamping(-1.0f);
  EXPECT(0.0f, reverb.GetDamping());
}

}  // namespace
