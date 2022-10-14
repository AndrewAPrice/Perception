// Copyright 2021 Google LLC
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

#include "virtual_file_system.h"

#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "memory_mapped_file.h"
#include "perception/fibers.h"
#include "perception/processes.h"

using ::file_systems::FileSystem;
using ::perception::Fiber;
using ::perception::GetCurrentlyExecutingFiber;
using ::perception::ProcessId;
using ::perception::Sleep;
using ::perception::Status;
using ::permebuf::perception::DirectoryEntryType;
using ::permebuf::perception::StorageManager;
using ::permebuf::perception::devices::StorageType;

namespace {

std::map<std::string, std::unique_ptr<FileSystem>, std::less<>>
    mounted_file_systems;

std::map<ProcessId, std::vector<std::unique_ptr<File>>>
    open_files_by_process_id;

std::map<ProcessId, std::vector<std::unique_ptr<MemoryMappedFile>>>
    open_memory_mapped_files_by_process_id;

// We save the first mounted file system, and shortcut /Applications,
// /Libraries into it.
std::string first_mounted_file_system = "";

std::vector<Fiber*> fibers_waiting_for_first_file_system;

int next_optical_drive_index = 1;
int next_unknown_device_index = 1;

std::string GetMountNameForFileSystem(FileSystem& file_system) {
  switch (file_system.GetStorageType()) {
    case StorageType::Optical: {
      std::string name = "Optical " + std::to_string(next_optical_drive_index);
      next_optical_drive_index++;
      return name;
    }
    default: {
      std::string name = std::to_string(next_unknown_device_index);
      next_unknown_device_index++;
      return name;
    }
  }
}

Status ExtractMountPointAndPath(std::string_view path,
                                std::string_view& mount_point,
                                std::string_view& path_on_mount_point) {
  if (path.empty() || path[0] != '/') {
    return Status::FILE_NOT_FOUND;
  }

  // Jump over the initial '/'.
  path = path.substr(1);

  // Find the split point (/) between the mount path and everything else.
  int split_point = path.find_first_of('/');

  if (split_point == std::string_view::npos) {
    mount_point = path;
    path_on_mount_point = "";
  } else {
    mount_point = path.substr(0, split_point);
    path_on_mount_point = path.substr(split_point + 1);
  }

  if (mount_point == "Libraries" || mount_point == "Applications") {
    if (first_mounted_file_system.empty()) {
      // Sleep until we have mounted a file system.
      fibers_waiting_for_first_file_system.push_back(
          GetCurrentlyExecutingFiber());
      Sleep();

      if (first_mounted_file_system.empty()) {
        std::cout << "We were rewoken with no first mounted file system."
                  << std::endl;
        return Status::FILE_NOT_FOUND;
      }
    }
    mount_point = first_mounted_file_system;
    path_on_mount_point = path;
  }

  return Status::OK;
}

StatusOr<std::unique_ptr<File>> OpenFileInternal(std::string_view path,
                                                 size_t& size_in_bytes,
                                                 ProcessId sender) {
  std::string_view mount_point, path_on_mount_point;
  RETURN_ON_ERROR(
      ExtractMountPointAndPath(path, mount_point, path_on_mount_point));

  // Does the mount point exist?
  auto mount_point_itr = mounted_file_systems.find(mount_point);
  if (mount_point_itr == mounted_file_systems.end()) {
    return Status::FILE_NOT_FOUND;  // No mount point.
  }

  return mount_point_itr->second->OpenFile(path_on_mount_point, size_in_bytes,
                                           sender);
}

}  // namespace

void MountFileSystem(std::unique_ptr<FileSystem> file_system) {
  std::string mount_name = GetMountNameForFileSystem(*file_system);
  std::cout << "Mounting " << file_system->GetFileSystemType() << " on "
            << file_system->GetDeviceName() << " as /" << mount_name << "/"
            << std::endl;
  mounted_file_systems[mount_name] = std::move(file_system);
  if (first_mounted_file_system.empty()) {
    first_mounted_file_system = mount_name;

    // Wake up the fibers waiting for the first file system.
    for (const auto fiber : fibers_waiting_for_first_file_system)
      fiber->WakeUp();
    fibers_waiting_for_first_file_system.clear();
  }
}

StatusOr<File*> OpenFile(std::string_view path, size_t& size_in_bytes,
                         ProcessId sender) {
  // Scan the directory within the file system.
  ASSIGN_OR_RETURN(auto file, OpenFileInternal(path, size_in_bytes, sender));
  File* file_ptr = file.get();

  auto itr = open_files_by_process_id.find(sender);
  if (itr == open_files_by_process_id.end()) {
    // First file open by this process.
    std::vector<std::unique_ptr<File>> files;
    files.push_back(std::move(file));
    open_files_by_process_id[sender] = std::move(files);
  } else {
    itr->second.push_back(std::move(file));
  }

  return file_ptr;
}

StatusOr<MemoryMappedFile*> OpenMemoryMappedFile(
    std::string_view path, ::perception::ProcessId sender) {
  size_t size_in_bytes;
  ASSIGN_OR_RETURN(auto file, OpenFileInternal(path, size_in_bytes, sender));

  auto mmfile = std::make_unique<MemoryMappedFile>(std::move(file),
                                                   size_in_bytes, sender);
  MemoryMappedFile* mmfile_ptr = mmfile.get();

  auto itr = open_memory_mapped_files_by_process_id.find(sender);
  if (itr == open_memory_mapped_files_by_process_id.end()) {
    // First memory mapped file open by this process.
    std::vector<std::unique_ptr<MemoryMappedFile>> mmfiles;
    mmfiles.push_back(std::move(mmfile));
    open_memory_mapped_files_by_process_id[sender] = std::move(mmfiles);
  } else {
    itr->second.push_back(std::move(mmfile));
  }

  return mmfile_ptr;
}

::perception::Status CheckFilePermissions(std::string_view path,
                                          bool& file_exists, bool& can_read,
                                          bool& can_write, bool& can_execute) {
  std::string_view mount_point, path_on_mount_point;
  RETURN_ON_ERROR(
      ExtractMountPointAndPath(path, mount_point, path_on_mount_point));

  if (mount_point.empty()) {
    // Querying '/'.
    file_exists = true;
    can_read = true;
    can_write = false;
    can_execute = true;
    return Status::OK;
  }

  // Does the mount point exist?
  auto mount_point_itr = mounted_file_systems.find(mount_point);
  if (mount_point_itr == mounted_file_systems.end()) {
    // Mount point not found.
    file_exists = false;
    can_read = false;
    can_write = false;
    can_execute = false;

    return Status::OK;
  }

  // Scan the directory within the file system.
  mount_point_itr->second->CheckFilePermissions(
      path_on_mount_point, file_exists, can_read, can_write, can_execute);

  return Status::OK;
}

void CloseFile(::perception::ProcessId sender, File* file) {
  auto itr = open_files_by_process_id.find(sender);
  if (itr == open_files_by_process_id.end()) {
    std::cout << "CloseFile() called but something went wrong as " << sender
              << " doesn't own any files." << std::endl;
    return;
  }

  // Iterate through the files owned by the sender.
  for (auto itr_2 = itr->second.begin(); itr_2 != itr->second.end(); itr_2++) {
    if (itr_2->get() == file) {
      // We found our file.
      itr->second.erase(itr_2);
      return;
    }
  }
}

void CloseMemoryMappedFile(::perception::ProcessId sender,
                           MemoryMappedFile* mmfile) {
  auto itr = open_memory_mapped_files_by_process_id.find(sender);
  if (itr == open_memory_mapped_files_by_process_id.end()) {
    std::cout << "CloseMemoryMappedFile() called but something went wrong as "
              << sender << " doesn't own any memory mapped files." << std::endl;
    return;
  }

  // Iterate through the memory mapped files owned by the sender.
  for (auto itr_2 = itr->second.begin(); itr_2 != itr->second.end(); itr_2++) {
    if (itr_2->get() == mmfile) {
      // We found our file.
      itr->second.erase(itr_2);
      return;
    }
  }
}

bool ForEachEntryInDirectory(
    std::string_view directory, int offset, int count,
    const std::function<void(std::string_view, DirectoryEntryType, size_t)>&
        on_each_entry) {
  if (directory.empty() || directory[0] != '/') return true;

  if (directory == "/") {
    int index = 0;
    // Iterating the root directory, return each mount point.
    for (const auto& mounted_file_system : mounted_file_systems) {
      if (count != 0 && index >= offset + count) {
        // We are terminating early, but there is still more to
        // iterate.
        return false;
      }
      if (index >= offset && (index < offset + count || count == 0)) {
        on_each_entry(mounted_file_system.first, DirectoryEntryType::Directory,
                      0);
      }
      index++;
    }
    return true;  // Nothing more to iterate.
  } else {
    // Split the path into the mount path and everything else.
    std::string_view mount_point, path_on_mount_point;
    ExtractMountPointAndPath(directory, mount_point, path_on_mount_point);

    // Does the mount point exist?
    auto mount_point_itr = mounted_file_systems.find(mount_point);
    if (mount_point_itr == mounted_file_systems.end())
      return true;  // No mount point.

    // Scan the directory within the file system.
    return mount_point_itr->second->ForEachEntryInDirectory(
        path_on_mount_point, offset, count, on_each_entry);
  }
}

StatusOr<StorageManager::GetFileStatisticsResponse> GetFileStatistics(
    std::string_view path) {
  if (path.empty() || path[0] != '/') {
    return StorageManager::GetFileStatisticsResponse();
  }

  if (path == "/") {
    StorageManager::GetFileStatisticsResponse response;
    response.SetExists(true);
    response.SetIsDirectory(true);
    return response;
  }

  std::string_view mount_point, path_on_mount_point;
  RETURN_ON_ERROR(
      ExtractMountPointAndPath(path, mount_point, path_on_mount_point));

  // Does the mount point exist?
  auto mount_point_itr = mounted_file_systems.find(mount_point);
  if (mount_point_itr == mounted_file_systems.end()) {
    return StorageManager::GetFileStatisticsResponse();  // No mount point.
  }

  return mount_point_itr->second->GetFileStatistics(path_on_mount_point);
}
