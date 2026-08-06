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

#include "track_manager.h"

struct WavExportOptions {
  int sample_rate = 44100;   // Sample rate in Hz (e.g. 44100, 48000, 22050, 96000).
  int bits_per_sample = 16;  // Bit depth (e.g. 8, 16, 24, 32).
  bool is_float = false;     // True for 32-bit IEEE float PCM.
};

// Renders all unmuted tracks in track_manager and writes a .wav file.
// Returns true on success, false on error.
bool ExportSongToWav(const std::string& file_path,
                     const TrackManager& track_manager,
                     const WavExportOptions& options);
