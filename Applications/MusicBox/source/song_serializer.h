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
#include <vector>

#include "nlohmann/json.hpp"
#include "track_manager.h"

struct SongInfo {
  std::string name;
  std::string description;
  std::string file_path;
};

// Sanitizes note_per_beat by clamping [1, 32] and rounding to nearest power of 2.
int SanitizeNotePerBeat(int value);

// Discovers available .song files on disk across asset & user directories
std::vector<SongInfo> GetAvailableSongs();

// Loads a song from a JSON file into TrackManager and populates metadata
bool LoadSongFromFile(const std::string& path, TrackManager& track_manager,
                      SongMetadata* metadata_out = nullptr);

// Saves current TrackManager state & metadata into a JSON file
bool SaveSongToFile(const std::string& path, const TrackManager& track_manager,
                    const SongMetadata& metadata);

// JSON conversion helpers
nlohmann::json SongToJson(const TrackManager& track_manager,
                          const SongMetadata& metadata);
bool SongFromJson(const nlohmann::json& j, TrackManager& track_manager,
                  SongMetadata* metadata_out = nullptr);
