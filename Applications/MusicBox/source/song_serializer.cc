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

#include "song_serializer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

using json = ::nlohmann::json;

namespace {


// Default filesystem search directories for locating preset song files.
constexpr const char* kSongSearchPaths[] = {
    "/Applications/MusicBox/songs", "/Applications/MusicBox/assets/songs",
    "Applications/MusicBox/assets/songs", "assets/songs", "./assets/songs"};

}  // namespace

std::vector<SongInfo> GetAvailableSongs() {
  std::vector<SongInfo> songs;
  std::set<std::string> seen_names;

  std::error_code ec;
  for (const auto* dir_path : kSongSearchPaths) {
    if (!std::filesystem::exists(dir_path, ec) ||
        !std::filesystem::is_directory(dir_path, ec)) {
      continue;
    }

    for (const auto& entry :
         std::filesystem::directory_iterator(dir_path, ec)) {
      if (ec) break;
      if (!entry.is_regular_file(ec)) continue;

      auto path = entry.path();
      if (path.extension() == ".song") {
        std::string filename = path.stem().string();
        if (seen_names.count(filename) > 0) continue;
        seen_names.insert(filename);

        SongInfo info;
        info.name = filename;
        info.file_path = path.string();

        // Try reading title & description metadata from JSON
        std::ifstream file(path.string());
        if (file.is_open()) {
          try {
            json j;
            file >> j;
            if (j.contains("title") && j["title"].is_string()) {
              info.name = j["title"].get<std::string>();
            }
            if (j.contains("description") && j["description"].is_string()) {
              info.description = j["description"].get<std::string>();
            }
          } catch (...) {
          }
        }

        songs.push_back(info);
      }
    }
  }

  // Sort songs alphabetically by display name
  std::sort(
      songs.begin(), songs.end(),
      [](const SongInfo& a, const SongInfo& b) { return a.name < b.name; });

  return songs;
}

nlohmann::json SongToJson(const TrackManager& track_manager,
                          const SongMetadata& metadata) {
  json j;
  j["title"] = metadata.title;
  j["description"] = metadata.description;
  j["bpm"] = track_manager.GetBpm();
  j["beats_per_bar"] = metadata.beats_per_bar;
  j["note_per_beat"] = SanitizeNotePerBeat(metadata.note_per_beat);

  json tracks_j = json::array();
  for (const auto& track : track_manager.GetTracks()) {
    json track_j;
    track_j["id"] = track.id;
    track_j["name"] = track.name;
    track_j["instrument"] =
        track.instrument ? track.instrument->name : "Acoustic Piano";
    track_j["volume"] = track.volume;
    track_j["muted"] = track.muted;
    track_j["color"] = track.color;
    switch (track.clef) {
      case Clef::Alto:
        track_j["clef"] = "alto";
        break;
      case Clef::Tenor:
        track_j["clef"] = "tenor";
        break;
      case Clef::TrebleAndBass:
      default:
        track_j["clef"] = "treble_and_bass";
        break;
    }

    json notes_j = json::array();
    for (const auto& note : track.notes) {
      json note_j;
      note_j["key_index"] = note.key_index;
      note_j["start_tick"] = note.start_tick;
      note_j["duration_ticks"] = note.duration_ticks;
      note_j["start_time_ms"] = note.start_time_ms;
      note_j["duration_ms"] = note.duration_ms;
      note_j["velocity"] = note.velocity;
      notes_j.push_back(note_j);
    }
    track_j["notes"] = notes_j;
    tracks_j.push_back(track_j);
  }
  j["tracks"] = tracks_j;

  return j;
}

bool SongFromJson(const nlohmann::json& j, TrackManager& track_manager,
                  SongMetadata* metadata_out) {
  if (!j.is_object()) return false;

  track_manager.ClearAllTracks();

  SongMetadata meta;
  if (j.contains("title") && j["title"].is_string()) {
    meta.title = j["title"].get<std::string>();
  }
  if (j.contains("description") && j["description"].is_string()) {
    meta.description = j["description"].get<std::string>();
  }
  if (j.contains("bpm") && j["bpm"].is_number()) {
    meta.bpm = j["bpm"].get<float>();
  } else {
    meta.bpm = 120.0f;
  }
  if (j.contains("beats_per_bar") && j["beats_per_bar"].is_number()) {
    meta.beats_per_bar = j["beats_per_bar"].get<int>();
  } else {
    meta.beats_per_bar = 4;
  }
  if (j.contains("note_per_beat") && j["note_per_beat"].is_number()) {
    meta.note_per_beat = SanitizeNotePerBeat(j["note_per_beat"].get<int>());
  } else {
    meta.note_per_beat = 4;
  }

  track_manager.SetBpm(meta.bpm);

  if (metadata_out) {
    *metadata_out = meta;
  }

  if (!j.contains("tracks") || !j["tracks"].is_array()) {
    return true;
  }

  int track_color_idx = 0;
  for (const auto& track_j : j["tracks"]) {
    if (!track_j.is_object()) continue;

    std::string track_name = track_j.value("name", "Track");
    std::string inst_name = track_j.value("instrument", "Acoustic Piano");
    const Instrument* inst = GetInstrument(inst_name);
    if (!inst) inst = GetDefaultInstrument();

    uint32 color =
        track_j.value("color", GetTrackPresetColor(track_color_idx++));
    Track* track = track_manager.AddTrack(track_name, inst, color);
    if (!track) continue;

    track->volume = track_j.value("volume", 0.8f);
    track->muted = track_j.value("muted", false);

    if (track_j.contains("clef") && track_j["clef"].is_string()) {
      std::string clef_str = track_j["clef"].get<std::string>();
      if (clef_str == "alto") {
        track->clef = Clef::Alto;
      } else if (clef_str == "tenor") {
        track->clef = Clef::Tenor;
      } else {
        track->clef = Clef::TrebleAndBass;
      }
    } else if (track_j.contains("clef") &&
               track_j["clef"].is_number_integer()) {
      int c_val = track_j["clef"].get<int>();
      if (c_val == 1)
        track->clef = Clef::Alto;
      else if (c_val == 2)
        track->clef = Clef::Tenor;
      else
        track->clef = Clef::TrebleAndBass;
    } else {
      track->clef = inst ? inst->default_clef : Clef::TrebleAndBass;
    }

    if (track_j.contains("notes") && track_j["notes"].is_array()) {
      for (const auto& note_j : track_j["notes"]) {
        if (!note_j.is_object()) continue;

        NoteEvent note;
        note.key_index = note_j.value("key_index", 60);
        note.start_tick = note_j.value("start_tick", 0);
        note.duration_ticks = note_j.value("duration_ticks", 32);
        note.start_time_ms =
            note_j.value("start_time_ms",
                         TrackManager::TicksToMs(note.start_tick, meta.bpm));
        note.duration_ms = note_j.value(
            "duration_ms",
            TrackManager::TicksToMs(note.duration_ticks, meta.bpm));
        note.velocity = note_j.value("velocity", 1.0f);

        track->notes.push_back(note);
      }
    }
    track_manager.SyncTrackNotesMs(*track);
  }

  return true;
}

bool LoadSongFromFile(const std::string& path, TrackManager& track_manager,
                      SongMetadata* metadata_out) {
  std::ifstream file(path);
  if (!file.is_open()) return false;

  try {
    json j;
    file >> j;
    return SongFromJson(j, track_manager, metadata_out);
  } catch (...) {
    return false;
  }
}

bool SaveSongToFile(const std::string& path, const TrackManager& track_manager,
                    const SongMetadata& metadata) {
  std::error_code ec;
  std::filesystem::path p(path);
  if (p.has_parent_path()) {
    std::filesystem::create_directories(p.parent_path(), ec);
  }

  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open()) return false;

  json j = SongToJson(track_manager, metadata);
  file << j.dump(2);
  return true;
}
