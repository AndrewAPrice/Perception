// Copyright 2024 Google LLC
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

#include "file.h"

#include <filesystem>
#include <iostream>
#include <memory>

#include "multiboot.h"
#include "perception/memory.h"
#include "perception/memory_mapped_file.h"
#include "perception/services.h"
#include "perception/storage_manager.h"
#include "status.h"

using ::perception::GetService;
using ::perception::kPageSize;
using ::perception::MemoryMappedFile;
using ::perception::MemorySpan;
using ::perception::ReleaseMemoryPages;
using ::perception::SharedMemory;
using ::perception::Status;
using ::perception::StorageManager;

namespace {

// Extracts the name of an application from a path.
// e.g. "/Applications/Calculator/Calculator.app" -> "Calculator"
std::string_view ExtractApplicationNameFromPath(std::string_view path) {
  // Remove the directories from the path name.
  auto slash_index = path.find_last_of('/');
  if (slash_index != std::string_view::npos)
    path = path.substr(slash_index + 1);

  // Remove the extension from the path name.
  auto dot_index = path.find_last_of('.');
  if (dot_index != std::string_view::npos) path = path.substr(0, dot_index);

  return path;
}

// Trims a library name.
// e.g. "libabc.so" -> "abc"
std::string_view GetTrimmedLibraryName(std::string_view library_name) {
  // Trim off the starting "lib" and ending ".so".
  if (library_name.size() >= 6 && library_name.substr(0, 3) == "lib" &&
      library_name.substr(library_name.size() - 3) == ".so")
    return library_name.substr(3, library_name.size() - 6);

  return library_name;
}

// Returns a path to a file.
std::optional<std::string> GetPathToFile(std::string_view name_sv) {
  if (name_sv.size() == 0) return std::nullopt;

  std::string name = std::string(name_sv);
  if (name[0] == '/') {
    // This is a fully qualified path. Check that it exists.
    if (!std::filesystem::exists(name)) return std::nullopt;
    return name;
  }
  // Check /Applications/ first, since it's the first mount point.
  std::string path_suffix, path;
  std::string_view trimmed_name = GetTrimmedLibraryName(name_sv);
  if (trimmed_name != name) {
    path_suffix = "/" + std::string(trimmed_name) + "/" + name;
    path = "/Libraries" + path_suffix;

  } else {
    path_suffix = "/" + name + "/" + name + ".app";
    path = "/Applications" + path_suffix;
  }
  if (std::filesystem::exists(path)) return path;

  // Check each mounted disk.
  for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
    std::string disk_path = std::string(root_entry.path()) + path;
    if (std::filesystem::exists(disk_path)) return disk_path;
  }

  // Can't find the application anywhere.
  return std::nullopt;
}

// Represents a file loaded from disk.
class DiskFile : public File {
 public:
  DiskFile(MemoryMappedFile::Client memory_mapped_file,
           std::shared_ptr<SharedMemory> shared_memory, std::string name,
           std::string path)
      : memory_mapped_file_(memory_mapped_file),
        shared_memory_(shared_memory),
        name_(name),
        path_(path) {
    memory_span_ = shared_memory_->ToSpan();
  }

  ~DiskFile() { memory_mapped_file_.Close(nullptr); }

  virtual const ::perception::MemorySpan MemorySpan() const override {
    return memory_span_;
  }

  virtual const std::string& Name() const override { return name_; }
  virtual const std::string& Path() const override { return path_; }

 private:
  // The underlying memory mapped file.
  MemoryMappedFile::Client memory_mapped_file_;

  // The shared memory block.
  std::shared_ptr<SharedMemory> shared_memory_;

  // The memory span wrapping the data in this file.
  class MemorySpan memory_span_;

  // The name of the file.
  std::string name_;

  // The path to the file.
  std::string path_;
};

// Represents a file loaded from a multiboot module.
class MultibootFile : public File {
 public:
  MultibootFile(std::unique_ptr<MultibootModule> module, std::string name)
      : module_(std::move(module)), name_(name) {
    path_ = std::string("Multiboot Module: ") + name_;
  }

  ~MultibootFile() {
    size_t pages = (module_->data.Length() + kPageSize - 1) / kPageSize;
    ReleaseMemoryPages(*module_->data, pages);
  }

  virtual const ::perception::MemorySpan MemorySpan() const override {
    return module_->data;
  }

  virtual const std::string& Name() const override { return name_; }
  virtual const std::string& Path() const override { return path_; }

 private:
  // The multiboot module that is this file.
  std::unique_ptr<MultibootModule> module_;

  // The name of the file.
  std::string name_;

  // The synthetic path to the file.
  std::string path_;
};

// Attempts to load a file from a multiboot module. Returns an empty pointer
// if the file cannot be loaded.
std::unique_ptr<File> LoadContentsFromMultibootModule(std::string_view name) {
  auto module = GetMultibootModule(name);
  if (!module) return nullptr;

  return std::make_unique<MultibootFile>(std::move(module), std::string(name));
}

// Attempts to load a file from a disk. Returns an empty pointer if the file
// cannot be loaded.
std::unique_ptr<File> LoadContentsFromDisk(std::string_view name) {
  if (IsLoadingMultibootModules()) {
    // The dependences for the multiboot modules must be passed in as other
    // multiboot modules and not loaded from disk, otherwise the system can get
    // into a deadlock waiting for a StorageManager.
    std::cout << "Unable to load \"" << name
              << "\". This should be passed in as a multiboot module."
              << std::endl;
    return nullptr;
  }

  auto opt_path = GetPathToFile(name);
  if (!opt_path) return nullptr;

  std::string_view path = *opt_path;

  if (path == name) {
    // The provided name was a fully qualified path. So, extract the name from
    // the path.
    name = ExtractApplicationNameFromPath(path);
  }

  // Open the file as a memory mapped file.
  auto status_or_response =
      GetService<StorageManager>().OpenMemoryMappedFile({path});
  if (!status_or_response.Ok()) return nullptr;
  auto response = std::move(*status_or_response);

  MemoryMappedFile::Client file = response.file;
  std::shared_ptr<SharedMemory> file_buffer = response.file_contents;

  return std::make_unique<DiskFile>(file, std::move(file_buffer),
                                    std::string(name), std::string(path));
}

}  // namespace

std::unique_ptr<File> LoadFile(std::string_view name) {
  auto file = LoadContentsFromMultibootModule(name);
  if (file) return file;

  file = LoadContentsFromDisk(name);
  if (file) return file;

  std::cout << "Cannot find \"" << name << "\" to load." << std::endl;
  return nullptr;
}
