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

#include "note_tree.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {

std::shared_ptr<NoteNode> FindNoteInList(
    const std::vector<std::shared_ptr<NoteNode>>& list, int id) {
  for (const auto& node : list) {
    if (!node) continue;
    if (node->id == id) return node;
    if (auto res = FindNoteInList(node->children, id)) return res;
  }
  return nullptr;
}

bool RemoveNoteFromList(std::vector<std::shared_ptr<NoteNode>>& list, int id) {
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (!*it) continue;
    if ((*it)->id == id) {
      list.erase(it);
      return true;
    }
    if (RemoveNoteFromList((*it)->children, id)) return true;
  }
  return false;
}

std::shared_ptr<NoteNode> CloneNodeRecursive(
    std::shared_ptr<NoteNode> src, NoteTree& tree,
    std::shared_ptr<NoteNode> parent_node) {
  if (!src) return nullptr;
  auto clone = std::make_shared<NoteNode>();
  clone->id = tree.next_id++;
  clone->name = src->name;
  clone->parent = parent_node;

  // Save duplicate content to disk
  std::string content = LoadNoteContentFromDisk(src->id);
  SaveNoteContentToDisk(clone->id, content);

  for (const auto& child : src->children) {
    auto child_clone = CloneNodeRecursive(child, tree, clone);
    if (child_clone) clone->children.push_back(child_clone);
  }
  return clone;
}

}  // namespace

nlohmann::json NoteNode::ToJson() const {
  nlohmann::json j;
  j["id"] = id;
  j["name"] = name;
  nlohmann::json children_j = nlohmann::json::array();
  for (const auto& child : children) {
    if (child) children_j.push_back(child->ToJson());
  }
  j["children"] = children_j;
  return j;
}

std::shared_ptr<NoteNode> NoteNode::FromJson(
    const nlohmann::json& j, std::shared_ptr<NoteNode> parent_node) {
  auto node = std::make_shared<NoteNode>();
  if (j.contains("id") && j["id"].is_number_integer()) {
    node->id = j["id"].get<int>();
  }
  if (j.contains("name") && j["name"].is_string()) {
    node->name = j["name"].get<std::string>();
  }
  node->parent = parent_node;
  if (j.contains("children") && j["children"].is_array()) {
    for (const auto& child_j : j["children"]) {
      auto child = FromJson(child_j, node);
      if (child) node->children.push_back(child);
    }
  }
  return node;
}

std::shared_ptr<NoteNode> NoteTree::FindNoteById(int id) const {
  return FindNoteInList(root_notes, id);
}

bool NoteTree::RemoveNote(int id) { return RemoveNoteFromList(root_notes, id); }

std::shared_ptr<NoteNode> NoteTree::DuplicateNote(
    std::shared_ptr<NoteNode> target, int& out_new_id) {
  if (!target) return nullptr;
  auto clone = CloneNodeRecursive(target, *this, target->parent.lock());
  if (!clone) return nullptr;
  clone->name = target->name + " (Copy)";
  out_new_id = clone->id;

  if (auto parent = target->parent.lock()) {
    parent->children.push_back(clone);
  } else {
    root_notes.push_back(clone);
  }
  return clone;
}

std::shared_ptr<NoteNode> NoteTree::EnsureAtLeastOneNote() {
  if (!root_notes.empty()) {
    return root_notes.front();
  }
  if (next_id < 0) next_id = 0;
  auto initial_note = std::make_shared<NoteNode>();
  initial_note->id = next_id++;
  initial_note->name = "Untitled note";
  root_notes.push_back(initial_note);
  SaveNoteContentToDisk(initial_note->id, "");
  return initial_note;
}

nlohmann::json NoteTree::ToJson() const {
  nlohmann::json j;
  j["next_id"] = next_id;
  nlohmann::json notes_j = nlohmann::json::array();
  for (const auto& note : root_notes) {
    if (note) notes_j.push_back(note->ToJson());
  }
  j["notes"] = notes_j;
  return j;
}

void NoteTree::FromJson(const nlohmann::json& j) {
  root_notes.clear();
  if (j.contains("next_id") && j["next_id"].is_number_integer()) {
    next_id = j["next_id"].get<int>();
  } else {
    next_id = 0;
  }

  if (j.contains("notes") && j["notes"].is_array()) {
    for (const auto& note_j : j["notes"]) {
      auto note = NoteNode::FromJson(note_j, nullptr);
      if (note) root_notes.push_back(note);
    }
  }
}

std::string GetNotesStorageDir() { return "/Applications/Notes/Notes"; }

bool EnsureNotesStorageDirExists() {
  std::error_code ec;
  std::string dir = GetNotesStorageDir();
  if (!std::filesystem::exists(dir, ec)) {
    return std::filesystem::create_directories(dir, ec);
  }
  return true;
}

bool LoadNotesTreeFromDisk(NoteTree& tree) {
  EnsureNotesStorageDirExists();
  std::string json_path = GetNotesStorageDir() + "/notes.json";

  std::ifstream file(json_path);
  if (!file.is_open()) {
    // Initialize default notes tree
    tree.EnsureAtLeastOneNote();
    SaveNotesTreeToDisk(tree);
    return true;
  }

  try {
    nlohmann::json j;
    file >> j;
    tree.FromJson(j);
  } catch (...) {
  }

  if (tree.root_notes.empty()) {
    tree.EnsureAtLeastOneNote();
    SaveNotesTreeToDisk(tree);
  }

  return true;
}

bool SaveNotesTreeToDisk(const NoteTree& tree) {
  EnsureNotesStorageDirExists();
  std::string json_path = GetNotesStorageDir() + "/notes.json";

  std::ofstream file(json_path, std::ios::trunc);
  if (!file.is_open()) return false;

  file << tree.ToJson().dump(2);
  return true;
}

std::string LoadNoteContentFromDisk(int note_id) {
  EnsureNotesStorageDirExists();
  std::string note_path =
      GetNotesStorageDir() + "/" + std::to_string(note_id) + ".txt";

  std::ifstream file(note_path, std::ios::binary);
  if (!file.is_open()) return "";

  std::stringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

bool SaveNoteContentToDisk(int note_id, const std::string& content) {
  EnsureNotesStorageDirExists();
  std::string note_path =
      GetNotesStorageDir() + "/" + std::to_string(note_id) + ".txt";

  std::ofstream file(note_path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) return false;

  file << content;
  return true;
}

void DeleteNoteFilesFromDisk(std::shared_ptr<NoteNode> note) {
  if (!note) return;
  std::error_code ec;
  std::string note_path =
      GetNotesStorageDir() + "/" + std::to_string(note->id) + ".txt";
  std::filesystem::remove(note_path, ec);

  for (const auto& child : note->children) {
    DeleteNoteFilesFromDisk(child);
  }
}
