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

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

uint16_t ReadUint16LE(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ReadUint32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

int32_t ReadInt24LE(const uint8_t* p) {
  uint32_t val = static_cast<uint32_t>(p[0]) |
                 (static_cast<uint32_t>(p[1]) << 8) |
                 (static_cast<uint32_t>(p[2]) << 16);
  if (val & 0x800000) val |= 0xFF000000;
  return static_cast<int32_t>(val);
}

}  // namespace

bool LoadWavFromMemory(const uint8_t* data, size_t size, WavData& out_wav) {
  if (!data || size < 44) return false;

  // Verify RIFF header
  if (std::memcmp(data, "RIFF", 4) != 0 ||
      std::memcmp(data + 8, "WAVE", 4) != 0) {
    return false;
  }

  uint16_t audio_format = 1;
  uint16_t num_channels = 2;
  uint32_t sample_rate = 44100;
  uint16_t bits_per_sample = 16;
  uint16_t block_align = 4;

  const uint8_t* data_chunk_ptr = nullptr;
  uint32_t data_chunk_size = 0;

  size_t offset = 12;
  while (offset + 8 <= size) {
    const char* chunk_id = reinterpret_cast<const char*>(data + offset);
    uint32_t chunk_size = ReadUint32LE(data + offset + 4);
    offset += 8;

    if (offset + chunk_size > size)
      chunk_size = static_cast<uint32_t>(size - offset);

    if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
      audio_format = ReadUint16LE(data + offset);
      num_channels = ReadUint16LE(data + offset + 2);
      sample_rate = ReadUint32LE(data + offset + 4);
      block_align = ReadUint16LE(data + offset + 12);
      bits_per_sample = ReadUint16LE(data + offset + 14);
    } else if (std::memcmp(chunk_id, "data", 4) == 0) {
      data_chunk_ptr = data + offset;
      data_chunk_size = chunk_size;
    }

    // Align chunk reading to 2 bytes
    offset += chunk_size + (chunk_size & 1);
  }

  if (!data_chunk_ptr || data_chunk_size == 0 || num_channels == 0 ||
      sample_rate == 0 || bits_per_sample == 0)
    return false;

  size_t bytes_per_frame = (bits_per_sample / 8) * num_channels;
  if (bytes_per_frame == 0) bytes_per_frame = block_align;
  if (bytes_per_frame == 0) return false;

  size_t total_frames = data_chunk_size / bytes_per_frame;
  if (total_frames == 0) return false;

  out_wav.sample_rate = sample_rate;
  out_wav.channels =
      (num_channels == 1) ? 1 : 2;  // standard output: mono or stereo
  out_wav.bits_per_sample = 16;
  out_wav.duration_seconds =
      static_cast<double>(total_frames) / static_cast<double>(sample_rate);
  out_wav.samples_mono.resize(total_frames);
  out_wav.pcm_bytes.resize(total_frames * out_wav.channels * sizeof(int16_t));

  int16_t* pcm_out = reinterpret_cast<int16_t*>(out_wav.pcm_bytes.data());

  for (size_t i = 0; i < total_frames; ++i) {
    const uint8_t* frame_ptr = data_chunk_ptr + i * bytes_per_frame;
    float sum_sample = 0.0f;

    for (size_t ch = 0; ch < std::min<size_t>(num_channels, 2); ++ch) {
      float sample_val = 0.0f;
      const uint8_t* sample_ptr = frame_ptr + ch * (bits_per_sample / 8);

      if (audio_format == 3) {  // IEEE Float
        if (bits_per_sample == 32) {
          float f;
          std::memcpy(&f, sample_ptr, sizeof(float));
          sample_val = f;
        }
      } else {  // PCM
        switch (bits_per_sample) {
          case 8: {
            uint8_t u8 = *sample_ptr;
            sample_val = (static_cast<float>(u8) - 128.0f) / 128.0f;
            break;
          }
          case 16: {
            int16_t s16 = static_cast<int16_t>(ReadUint16LE(sample_ptr));
            sample_val = static_cast<float>(s16) / 32768.0f;
            break;
          }
          case 24: {
            int32_t s24 = ReadInt24LE(sample_ptr);
            sample_val = static_cast<float>(s24) / 8388608.0f;
            break;
          }
          case 32: {
            int32_t s32 = static_cast<int32_t>(ReadUint32LE(sample_ptr));
            sample_val = static_cast<float>(s32) / 2147483648.0f;
            break;
          }
        }
      }

      sample_val = std::clamp(sample_val, -1.0f, 1.0f);
      sum_sample += sample_val;

      // Convert to 16-bit PCM output sample
      int16_t pcm_s16 = static_cast<int16_t>(
          std::clamp(sample_val * 32767.0f, -32767.0f, 32767.0f));
      if (out_wav.channels == 1) {
        pcm_out[i] = pcm_s16;
      } else {
        pcm_out[i * 2 + ch] = pcm_s16;
      }
    }

    float mono_val =
        sum_sample / static_cast<float>(std::min<size_t>(num_channels, 2));
    out_wav.samples_mono[i] = mono_val;
  }

  return true;
}

bool LoadWavFile(std::string_view path, WavData& out_wav) {
  std::ifstream file(std::string(path).c_str(),
                     std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  std::streamsize file_size = file.tellg();
  if (file_size <= 0) return false;

  file.seekg(0, std::ios::beg);
  std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
  if (!file.read(reinterpret_cast<char*>(buffer.data()), file_size)) {
    return false;
  }

  return LoadWavFromMemory(buffer.data(), buffer.size(), out_wav);
}
