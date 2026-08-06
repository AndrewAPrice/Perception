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

#include <string>
#include <string_view>
#include <vector>

#include "instruments.h"
#include "perception/audio.h"
#include "perception/shared_memory.h"
#include "types.h"

// Converts key index (0 to 87) to frequency in Hz. Key 0 is A0 (27.5 Hz), Key
// 48 is A4 (440 Hz).
double KeyIndexToFrequency(int key_index);

// Returns note name string (e.g. "A0", "C4", "F#5") for key index 0 to 87.
std::string KeyIndexToNoteName(int key_index);

// Synthesizes a raw PCM 16-bit audio sample buffer for a given note.
std::vector<int16> SynthesizeNoteBuffer(int key_index,
                                        const Instrument* instrument,
                                        double volume = 1.0,
                                        double duration_seconds = 1.2);

// Algorithmically plays a note immediately using Perception's AudioDevice.
::perception::AudioStreamID PlaySynthesizedNote(int key_index,
                                                const Instrument* instrument,
                                                double volume = 1.0,
                                                double duration_seconds = 1.2);

// Initializes the synth engine and queries the audio device for its sample
// rate.
void InitializeSynthEngine();

// Starts the continuous real-time audio streaming background fiber.
void StartAudioStream();

// Stops the audio streaming background fiber.
void StopAudioStream();

// Triggers a note on in the user-space voice pool immediately (<10ms latency).
void NoteOn(int key_index, const Instrument* instrument, double volume = 0.8);

// Releases a currently playing note in the user-space voice pool.
void NoteOff(int key_index);

// Plays a timed note (e.g. for song playback) in the voice pool.
void PlayNote(int key_index, const Instrument* instrument, double volume = 0.8,
              double duration_seconds = 0.5);

// Reverb FX Engine controls
void SetReverbEnabled(bool enabled);
bool IsReverbEnabled();
void SetReverbMix(float mix);
float GetReverbMix();
void SetReverbRoomSize(float room_size);
float GetReverbRoomSize();
void SetReverbDamping(float damping);
float GetReverbDamping();
