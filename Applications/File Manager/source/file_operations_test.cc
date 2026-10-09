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

#include "file_operations.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "testing.h"

namespace {

class ScopedTempDir {
 public:
  ScopedTempDir() {
    auto base = std::filesystem::temp_directory_path();
    path_ = (base / ("file_manager_test_" + std::to_string(getpid()) + "_" +
                     std::to_string(counter_++)))
                .string();
    std::filesystem::create_directories(path_);
  }

  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  const std::string& path() const { return path_; }

 private:
  static inline int counter_ = 0;
  std::string path_;
};

void WriteTextFile(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << content;
}

std::string ReadTextFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

TEST(FileOperations_GetExtension) {
  EXPECT(std::string("txt"), GetExtension("notes.txt"));
  EXPECT(std::string("app"), GetExtension("MusicBox.APP"));
  EXPECT(std::string("gz"), GetExtension("archive.tar.gz"));
  EXPECT(std::string(""), GetExtension("README"));
  EXPECT(std::string(""), GetExtension(".gitignore"));
}

TEST(FileOperations_JoinPath) {
  EXPECT(std::string("/Documents"), JoinPath("/", "Documents"));
  EXPECT(std::string("/Documents"), JoinPath("", "Documents"));
  EXPECT(std::string("/Documents/file.txt"),
         JoinPath("/Documents", "file.txt"));
  EXPECT(std::string("/Documents/file.txt"),
         JoinPath("/Documents/", "file.txt"));
}

TEST(FileOperations_NormalizeAndResolveDirectoryPath) {
  EXPECT(std::string("/"), NormalizeAndResolveDirectoryPath(""));
  EXPECT(std::string("/"), NormalizeAndResolveDirectoryPath("/"));
  EXPECT(std::string("/Applications"),
         NormalizeAndResolveDirectoryPath("/Applications/"));
}

TEST(FileOperations_GetUniquePath) {
  ScopedTempDir temp;
  std::string first = GetUniquePath(temp.path(), "Untitled.txt");
  EXPECT(JoinPath(temp.path(), "Untitled.txt"), first);

  WriteTextFile(first, "hello");
  std::string second = GetUniquePath(temp.path(), "Untitled.txt");
  EXPECT(JoinPath(temp.path(), "Untitled (1).txt"), second);

  WriteTextFile(second, "world");
  std::string third = GetUniquePath(temp.path(), "Untitled.txt");
  EXPECT(JoinPath(temp.path(), "Untitled (2).txt"), third);

  std::string forced = GetUniquePath(temp.path(), "Fresh.txt", true);
  EXPECT(JoinPath(temp.path(), "Fresh (1).txt"), forced);
}

TEST(FileOperations_CopyMoveAndDeleteRecursive) {
  ScopedTempDir temp;
  std::string src_dir = JoinPath(temp.path(), "src_folder");
  std::string nested_dir = JoinPath(src_dir, "nested");
  std::filesystem::create_directories(nested_dir);
  WriteTextFile(JoinPath(src_dir, "root.txt"), "root-data");
  WriteTextFile(JoinPath(nested_dir, "child.txt"), "child-data");

  std::string copied_dir = JoinPath(temp.path(), "copied_folder");
  std::string error_message;
  EXPECT(true, CopyItemRecursive(src_dir, copied_dir, error_message));
  EXPECT(std::string("root-data"),
         ReadTextFile(JoinPath(copied_dir, "root.txt")));
  EXPECT(std::string("child-data"),
         ReadTextFile(JoinPath(JoinPath(copied_dir, "nested"), "child.txt")));

  std::string moved_dir = JoinPath(temp.path(), "moved_folder");
  EXPECT(true, MoveOrRenameItem(copied_dir, moved_dir, error_message));
  EXPECT(false, std::filesystem::exists(copied_dir));
  EXPECT(std::string("child-data"),
         ReadTextFile(JoinPath(JoinPath(moved_dir, "nested"), "child.txt")));

  EXPECT(true, DeleteItemRecursive(moved_dir, error_message));
  EXPECT(false, std::filesystem::exists(moved_dir));
}

TEST(FileOperations_ReadAndSortDirectoryEntries) {
  ScopedTempDir temp;
  std::filesystem::create_directory(JoinPath(temp.path(), "zeta_folder"));
  std::filesystem::create_directory(JoinPath(temp.path(), "alpha_folder"));
  std::filesystem::create_directory(JoinPath(temp.path(), "MyApp.app"));
  WriteTextFile(JoinPath(temp.path(), "b_small.txt"), "12");
  WriteTextFile(JoinPath(temp.path(), "a_large.txt"), "1234567890");
  WriteTextFile(JoinPath(temp.path(), ".hidden"), "secret");

  auto by_name_asc = ReadDirectoryEntries(temp.path(), false, SortColumn::NAME,
                                          SortDirection::ASCENDING);
  EXPECT(static_cast<size_t>(5), by_name_asc.size());
  EXPECT(std::string("alpha_folder"),
         by_name_asc[0].path().filename().string());
  EXPECT(std::string("zeta_folder"),
         by_name_asc[1].path().filename().string());
  EXPECT(std::string("MyApp.app"), by_name_asc[2].path().filename().string());
  EXPECT(std::string("a_large.txt"),
         by_name_asc[3].path().filename().string());
  EXPECT(std::string("b_small.txt"),
         by_name_asc[4].path().filename().string());

  auto by_size_desc = ReadDirectoryEntries(temp.path(), true, SortColumn::SIZE,
                                           SortDirection::DESCENDING);
  EXPECT(static_cast<size_t>(6), by_size_desc.size());
  EXPECT(std::string("a_large.txt"),
         by_size_desc[3].path().filename().string());
}

}  // namespace
