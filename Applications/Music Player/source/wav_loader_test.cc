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

#include "wav_loader.h"

#include <cstring>
#include <vector>

#include "testing.h"

namespace {

void WriteUint16LE(std::vector<uint8_t>& buf, uint16_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void WriteUint32LE(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void WriteFourCC(std::vector<uint8_t>& buf, const char* fourcc) {
  buf.push_back(fourcc[0]);
  buf.push_back(fourcc[1]);
  buf.push_back(fourcc[2]);
  buf.push_back(fourcc[3]);
}

std::vector<uint8_t> CreateDummyWav(uint32_t sample_rate, uint16_t channels,
                                    uint16_t bits_per_sample, size_t num_frames) {
  std::vector<uint8_t> buf;
  size_t bytes_per_sample = bits_per_sample / 8;
  size_t data_size = num_frames * channels * bytes_per_sample;
  size_t file_size_minus_8 = 36 + data_size;

  WriteFourCC(buf, "RIFF");
  WriteUint32LE(buf, static_cast<uint32_t>(file_size_minus_8));
  WriteFourCC(buf, "WAVE");

  WriteFourCC(buf, "fmt ");
  WriteUint32LE(buf, 16);  // fmt size
  WriteUint16LE(buf, 1);   // PCM
  WriteUint16LE(buf, channels);
  WriteUint32LE(buf, sample_rate);
  WriteUint32LE(buf, sample_rate * channels * bytes_per_sample);  // byte rate
  WriteUint16LE(buf, static_cast<uint16_t>(channels * bytes_per_sample));  // block align
  WriteUint16LE(buf, bits_per_sample);

  WriteFourCC(buf, "data");
  WriteUint32LE(buf, static_cast<uint32_t>(data_size));

  for (size_t i = 0; i < num_frames; ++i) {
    for (size_t c = 0; c < channels; ++c) {
      if (bits_per_sample == 16) {
        int16_t val = static_cast<int16_t>(i * 100);
        WriteUint16LE(buf, static_cast<uint16_t>(val));
      } else if (bits_per_sample == 8) {
        buf.push_back(128);
      }
    }
  }

  return buf;
}

TEST(WavLoader_ValidPcm16) {
  auto wav_bytes = CreateDummyWav(44100, 2, 16, 44100);  // 1 second of audio
  WavData wav;
  bool success = LoadWavFromMemory(wav_bytes.data(), wav_bytes.size(), wav);

  EXPECT(true, success);
  EXPECT(44100u, wav.sample_rate);
  EXPECT(2u, wav.channels);
  EXPECT(1.0, wav.duration_seconds);
  EXPECT(44100u, wav.samples_mono.size());
}

TEST(WavLoader_InvalidHeader) {
  std::vector<uint8_t> dummy = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  WavData wav;
  bool success = LoadWavFromMemory(dummy.data(), dummy.size(), wav);
  EXPECT(false, success);
}

}  // namespace
