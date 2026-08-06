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

#include "wav_exporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>

#include "instruments.h"
#include "reverb.h"
#include "synth_engine.h"

namespace {

void WriteUint32LE(std::vector<uint8_t>& buf, uint32_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
}

void WriteUint16LE(std::vector<uint8_t>& buf, uint16_t val) {
  buf.push_back(static_cast<uint8_t>(val & 0xFF));
  buf.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
}

void WriteFourCC(std::vector<uint8_t>& buf, const char* fourcc) {
  buf.push_back(static_cast<uint8_t>(fourcc[0]));
  buf.push_back(static_cast<uint8_t>(fourcc[1]));
  buf.push_back(static_cast<uint8_t>(fourcc[2]));
  buf.push_back(static_cast<uint8_t>(fourcc[3]));
}

}  // namespace

bool ExportSongToWav(const std::string& file_path,
                     const TrackManager& track_manager,
                     const WavExportOptions& options) {
  if (file_path.empty()) return false;
  if (options.sample_rate <= 0) return false;

  int bits_per_sample = options.bits_per_sample;
  if (bits_per_sample != 8 && bits_per_sample != 16 && bits_per_sample != 24 &&
      bits_per_sample != 32) {
    bits_per_sample = 16;
  }

  int song_dur_ms = track_manager.GetSongDurationMs();
  if (song_dur_ms <= 0) {
    song_dur_ms = 2000;  // Default 2 seconds if no notes exist
  }

  double total_duration_sec = (song_dur_ms / 1000.0) + 1.5;
  int total_frames =
      static_cast<int>(total_duration_sec * options.sample_rate);
  if (total_frames <= 0) return false;

  std::vector<double> mix(total_frames, 0.0);

  const auto& tracks = track_manager.GetTracks();
  for (const auto& track : tracks) {
    if (track.muted) continue;

    float track_vol = std::clamp(track.volume, 0.0f, 1.0f);
    const Instrument* inst =
        track.instrument ? track.instrument : GetDefaultInstrument();
    if (!inst) continue;

    double max_sustain = inst->max_sustain_seconds;
    double release_dur = inst->release_duration_seconds;
    bool ignore_off = inst->ignore_note_off;

    for (const auto& note : track.notes) {
      double freq = TrackManager::KeyIndexToFrequency(note.key_index);
      double note_start_sec = note.start_time_ms / 1000.0;
      double note_dur_sec = note.duration_ms / 1000.0;
      double note_total_sec =
          ignore_off ? std::min(max_sustain, 4.0) : (note_dur_sec + release_dur);

      int start_frame =
          static_cast<int>(note_start_sec * options.sample_rate);
      int note_frames =
          static_cast<int>(note_total_sec * options.sample_rate);
      double velocity =
          std::clamp(static_cast<double>(note.velocity), 0.0, 1.0);
      double note_vol = track_vol * velocity;

      double dt = 1.0 / options.sample_rate;
      double sample_block[128];

      for (int chunk_start = 0; chunk_start < note_frames; chunk_start += 128) {
        int chunk_size = std::min(128, note_frames - chunk_start);
        double t_start = static_cast<double>(chunk_start) / options.sample_rate;
        SynthesizeInstrumentBlock(inst, freq, t_start, dt, note_dur_sec,
                                  sample_block, chunk_size);

        for (int i = 0; i < chunk_size; ++i) {
          int frame_idx = start_frame + chunk_start + i;
          if (frame_idx >= total_frames) break;

          double t = t_start + i * dt;
          double sample = sample_block[i];
          if (!ignore_off && t > note_dur_sec) {
            double fade = 1.0 - ((t - note_dur_sec) / release_dur);
            if (fade < 0.0) fade = 0.0;
            sample *= fade;
          }

          double val = sample * note_vol;
          mix[frame_idx] += val;
        }
      }
    }
  }

  if (IsReverbEnabled()) {
    Reverb export_reverb;
    export_reverb.SetSampleRate(options.sample_rate);
    export_reverb.SetMix(GetReverbMix());
    export_reverb.SetRoomSize(GetReverbRoomSize());
    export_reverb.SetDamping(GetReverbDamping());
    export_reverb.Process(mix.data(), total_frames);
  }

  // Calculate peak level for normalization / master scaling
  double peak = 0.0;
  for (int i = 0; i < total_frames; ++i) {
    peak = std::max(peak, std::abs(mix[i]));
  }

  double master_gain = 0.8;
  if (peak > 0.95) {
    master_gain = 0.95 / peak;
  }

  // Construct WAV Header & Payload
  uint16_t format_tag = options.is_float ? 3 : 1;
  uint16_t num_channels = 1;
  uint32_t sample_rate = static_cast<uint32_t>(options.sample_rate);
  uint16_t bytes_per_sample_channel = static_cast<uint16_t>(bits_per_sample / 8);
  uint16_t block_align = num_channels * bytes_per_sample_channel;
  uint32_t byte_rate = sample_rate * block_align;
  uint32_t data_size = static_cast<uint32_t>(total_frames * block_align);
  uint32_t file_size_minus_8 = 36 + data_size;

  std::vector<uint8_t> wav_bytes;
  wav_bytes.reserve(44 + data_size);

  // RIFF Header
  WriteFourCC(wav_bytes, "RIFF");
  WriteUint32LE(wav_bytes, file_size_minus_8);
  WriteFourCC(wav_bytes, "WAVE");

  // fmt Subchunk
  WriteFourCC(wav_bytes, "fmt ");
  WriteUint32LE(wav_bytes, 16);  // fmt chunk size (16 for PCM)
  WriteUint16LE(wav_bytes, format_tag);
  WriteUint16LE(wav_bytes, num_channels);
  WriteUint32LE(wav_bytes, sample_rate);
  WriteUint32LE(wav_bytes, byte_rate);
  WriteUint16LE(wav_bytes, block_align);
  WriteUint16LE(wav_bytes, static_cast<uint16_t>(bits_per_sample));

  // data Subchunk
  WriteFourCC(wav_bytes, "data");
  WriteUint32LE(wav_bytes, data_size);

  // Sample Encoding
  for (int i = 0; i < total_frames; ++i) {
    double val = std::clamp(mix[i] * master_gain, -1.0, 1.0);

    if (options.is_float && bits_per_sample == 32) {
      float f_val = static_cast<float>(val);
      uint32_t u_val;
      std::memcpy(&u_val, &f_val, sizeof(float));
      WriteUint32LE(wav_bytes, u_val);
    } else if (bits_per_sample == 8) {
      uint8_t u8 = static_cast<uint8_t>(
          std::clamp((val * 127.0) + 128.0, 0.0, 255.0));
      wav_bytes.push_back(u8);
    } else if (bits_per_sample == 16) {
      int16_t s16 = static_cast<int16_t>(
          std::clamp(val * 32767.0, -32768.0, 32767.0));
      WriteUint16LE(wav_bytes, static_cast<uint16_t>(s16));
    } else if (bits_per_sample == 24) {
      int32_t s24 = static_cast<int32_t>(
          std::clamp(val * 8388607.0, -8388608.0, 8388607.0));
      wav_bytes.push_back(static_cast<uint8_t>(s24 & 0xFF));
      wav_bytes.push_back(static_cast<uint8_t>((s24 >> 8) & 0xFF));
      wav_bytes.push_back(static_cast<uint8_t>((s24 >> 16) & 0xFF));
    } else if (bits_per_sample == 32) {
      int32_t s32 = static_cast<int32_t>(
          std::clamp(val * 2147483647.0, -2147483648.0, 2147483647.0));
      WriteUint32LE(wav_bytes, static_cast<uint32_t>(s32));
    }
  }

  std::ofstream out(file_path, std::ios::binary);
  if (!out.is_open()) return false;

  out.write(reinterpret_cast<const char*>(wav_bytes.data()), wav_bytes.size());
  return out.good();
}
