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

#include <cstdio>
#include <fstream>
#include <vector>

#include "testing.h"
#include "track_manager.h"

namespace {

TEST(WavExporter_BasicExport) {
  TrackManager tm;
  Track* track = tm.GetActiveTrack();
  ASSERT(true, track != nullptr);

  // Add a note
  track->notes.push_back(
      NoteEvent{.key_index = 40,
                .start_tick = 0,
                .duration_ticks = 64,
                .start_time_ms = 0,
                .duration_ms = 500,
                .velocity = 0.8f});

  std::string test_path = "/tmp/test_export.wav";

  WavExportOptions options;
  options.sample_rate = 44100;
  options.bits_per_sample = 16;
  options.is_float = false;

  bool result = ExportSongToWav(test_path, tm, options);
  EXPECT(true, result);

  // Validate written file
  std::ifstream in(test_path, std::ios::binary);
  EXPECT(true, in.is_open());

  in.seekg(0, std::ios::end);
  size_t file_size = in.tellg();
  EXPECT(true, file_size > 44);

  // Read header
  in.seekg(0, std::ios::beg);
  char header[44];
  in.read(header, 44);

  // RIFF tag
  EXPECT('R', header[0]);
  EXPECT('I', header[1]);
  EXPECT('F', header[2]);
  EXPECT('F', header[3]);

  // WAVE tag
  EXPECT('W', header[8]);
  EXPECT('A', header[9]);
  EXPECT('V', header[10]);
  EXPECT('E', header[11]);

  // Number of channels (uint16_t at offset 22)
  uint16_t num_channels = static_cast<uint8_t>(header[22]) |
                          (static_cast<uint8_t>(header[23]) << 8);
  EXPECT(1, static_cast<int>(num_channels));

  std::remove(test_path.c_str());
}

TEST(WavExporter_FormatOptions) {
  TrackManager tm;

  std::string test_path_24 = "/tmp/test_export_24.wav";
  WavExportOptions opt24;
  opt24.sample_rate = 48000;
  opt24.bits_per_sample = 24;

  EXPECT(true, ExportSongToWav(test_path_24, tm, opt24));
  std::remove(test_path_24.c_str());

  std::string test_path_float = "/tmp/test_export_float.wav";
  WavExportOptions opt_float;
  opt_float.sample_rate = 48000;
  opt_float.bits_per_sample = 32;
  opt_float.is_float = true;

  EXPECT(true, ExportSongToWav(test_path_float, tm, opt_float));
  std::remove(test_path_float.c_str());
}

}  // namespace
