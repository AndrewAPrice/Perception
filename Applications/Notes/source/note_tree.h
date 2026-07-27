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

#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

struct NoteNode : public std::enable_shared_from_this<NoteNode> {
  int id = 0;
  std::string name = "Untitled note";
  bool is_dirty = false;
  std::vector<std::shared_ptr<NoteNode>> children;
  std::weak_ptr<NoteNode> parent;

  nlohmann::json ToJson() const;
  static std::shared_ptr<NoteNode> FromJson(
      const nlohmann::json& j, std::shared_ptr<NoteNode> parent_node = nullptr);
};

struct NoteTree {
  int next_id = 0;
  std::vector<std::shared_ptr<NoteNode>> root_notes;

  std::shared_ptr<NoteNode> FindNoteById(int id) const;
  bool RemoveNote(int id);
  std::shared_ptr<NoteNode> DuplicateNote(std::shared_ptr<NoteNode> target,
                                          int& out_new_id);
  std::shared_ptr<NoteNode> EnsureAtLeastOneNote();

  nlohmann::json ToJson() const;
  void FromJson(const nlohmann::json& j);
};

std::string GetNotesStorageDir();
bool EnsureNotesStorageDirExists();
bool LoadNotesTreeFromDisk(NoteTree& tree);
bool SaveNotesTreeToDisk(const NoteTree& tree);

std::string LoadNoteContentFromDisk(int note_id);
bool SaveNoteContentToDisk(int note_id, const std::string& content);
void DeleteNoteFilesFromDisk(std::shared_ptr<NoteNode> note);
