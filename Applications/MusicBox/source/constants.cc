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

#include "constants.h"

int GetDiatonicStep(int key_index) {
  int note_in_octave = key_index - kKeyOffsetC;
  int octave = 0;
  if (note_in_octave >= 0) {
    octave = note_in_octave / kSemitonesPerOctave;
    note_in_octave %= kSemitonesPerOctave;
  } else {
    octave = (note_in_octave - kOctaveFloorOffset) / kSemitonesPerOctave;
    note_in_octave =
        (note_in_octave % kSemitonesPerOctave + kSemitonesPerOctave) %
        kSemitonesPerOctave;
  }
  return octave * kDiatonicStepsPerOctave + kDiatonicStepFromC[note_in_octave];
}
