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

#include "types.h"

// Diatonic step offset relative to C for each semitone in an octave.
constexpr int kDiatonicStepFromC[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};

// Key index offset relative to C1 (A0 is key 0, C1 is key 3).
constexpr int kKeyOffsetC = 3;

// Total number of semitones in an octave.
constexpr int kSemitonesPerOctave = 12;

// Total number of diatonic steps (white keys) in an octave.
constexpr int kDiatonicStepsPerOctave = 7;

// Total number of keys on a standard piano keyboard.
constexpr int kTotalPianoKeys = 88;

// Maximum key index (0 to 87).
constexpr int kMaxKeyIndex = 87;

// Total number of white keys on a standard 88-key piano keyboard.
constexpr float kTotalWhiteKeys = 52.0f;

// Offset for rounding negative octaves downward in integer division.
constexpr int kOctaveFloorOffset = 11;

// Diatonic step index for E4 (bottom line of treble staff).
constexpr int kDiatonicStepE4 = 30;

// Diatonic step index for Middle C (C4).
constexpr int kDiatonicStepMiddleC = 28;

// Ticks per beat in standard 1/256th note tick timing.
constexpr int kTicksPerBeat = 64;

// Default fallback beats per minute (BPM).
constexpr float kDefaultBpm = 120.0f;

// Number of seconds per minute.
constexpr float kSecondsPerMinute = 60.0f;

// Number of milliseconds per minute.
constexpr float kMsPerMinute = 60000.0f;

// Number of milliseconds per second.
constexpr double kMsPerSecond = 1000.0;

// Default line and text color (Black ARGB).
constexpr uint32 kDefaultLineColor = 0xFF000000;

// Default velocity assigned to newly created notes.
constexpr float kDefaultNoteVelocity = 0.9f;

// Calculates the diatonic step index for a given MIDI key index (0 to 87).
int GetDiatonicStep(int key_index);
