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

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "types.h"

struct WavData {
  uint32 sample_rate = 44100;
  uint16 channels = 2;
  uint16 bits_per_sample = 16;
  double duration_seconds = 0.0;
  std::vector<float> samples_mono;  // Normalized [-1.0, 1.0] for waveform view
  std::vector<uint8_t> pcm_bytes;   // 16-bit PCM buffer for PlayAudio
};

// Loads and parses a RIFF WAVE file from disk.
bool LoadWavFile(std::string_view path, WavData& out_wav);

// Parses a RIFF WAVE file from an in-memory buffer.
bool LoadWavFromMemory(const uint8_t* data, size_t size, WavData& out_wav);
