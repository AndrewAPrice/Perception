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

#include "multiboot.h"
#include "perception/memory.h"
#include "permebuf/Libraries/perception/storage_manager.permebuf.h"
#include "status.h"

using ::perception::kPageSize;
using ::perception::MemorySpan;
using ::perception::ReleaseMemoryPages;
using ::perception::SharedMemory;
using ::perception::Status;
using ::permebuf::perception::MemoryMappedFile;
using ::permebuf::perception::StorageManager;

namespace {

bool found_an_instance_of_the_storage_manager = false;

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

std::string_view GetTrimmedLibraryName(std::string_view library_name) {
  // Trim off the starting "lib" and ending ".so".
  if (library_name.size() >= 6 && library_name.substr(0, 3) == "lib" &&
      library_name.substr(library_name.size() - 3) == ".so")
    return library_name.substr(3, library_name.size() - 6);

  return library_name;
}

std::optional<std::string> GetPathToApplication(std::string_view name_sv) {
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

class DiskFile : public File {
 public:
  DiskFile(MemoryMappedFile memory_mapped_file, SharedMemory shared_memory,
           std::string name, std::string path)
      : memory_mapped_file_(memory_mapped_file),
        shared_memory_(std::move(shared_memory)),
        name_(name),
        path_(path) {
    memory_span_ = shared_memory_.ToSpan();
  }

  ~DiskFile() {
    memory_mapped_file_.SendCloseFile(MemoryMappedFile::CloseFileMessage());
  }

  virtual const ::perception::MemorySpan MemorySpan() const override {
    return memory_span_;
  }

  virtual const std::string& Name() const override { return name_; }
  virtual const std::string& Path() const override { return path_; }

 private:
  MemoryMappedFile memory_mapped_file_;
  SharedMemory shared_memory_;
  class MemorySpan memory_span_;
  std::string name_;
  std::string path_;
};

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
  std::unique_ptr<MultibootModule> module_;
  std::string name_;
  std::string path_;
};

std::unique_ptr<File> LoadContentsFromMultibootModule(std::string_view name) {
  auto module = GetMultibootModule(name);
  if (!module) return nullptr;

  return std::make_unique<MultibootFile>(std::move(module), std::string(name));
}

std::unique_ptr<File> LoadContentsFromDisk(std::string_view name) {
  auto opt_path = GetPathToApplication(name);
  if (!opt_path) return nullptr;

  std::string_view path = *opt_path;

  if (path == name) {
    // The provided name was a fully qualified path. So, extract the name from
    // the path.
    name = ExtractApplicationNameFromPath(path);
  }

  // Open the ELf as a memory mapped file.
  Permebuf<StorageManager::OpenMemoryMappedFileRequest> request;
  request->SetPath(path);

  auto status_or_response =
      StorageManager::Get().CallOpenMemoryMappedFile(std::move(request));
  if (!status_or_response.Ok()) return nullptr;
  auto response = std::move(*status_or_response);

  MemoryMappedFile file = response.GetFile();
  SharedMemory file_buffer = response.GetFileContents().Clone();

  return std::make_unique<DiskFile>(file, std::move(file_buffer),
                                    std::string(name), std::string(path));
}

}  // namespace

std::unique_ptr<File> GetExecutableFile(std::string_view name) {
  auto file = LoadContentsFromMultibootModule(name);
  if (file) return file;

  if (!found_an_instance_of_the_storage_manager) {
    found_an_instance_of_the_storage_manager =
        StorageManager::FindFirstInstance().has_value();
    if (!found_an_instance_of_the_storage_manager) {
      std::cout << "Cannot find \"" << name
                << "\" to load and there is no storage manager." << std::endl;
      return nullptr;
    }
  }
  file = LoadContentsFromDisk(name);
  if (file) return file;

  std::cout << "Cannot find \"" << name << "\" to load." << std::endl;
  return nullptr;
}
