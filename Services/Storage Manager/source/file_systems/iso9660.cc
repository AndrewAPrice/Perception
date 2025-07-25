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

#include "file_systems/iso9660.h"

#include <iostream>

#include "perception/scheduler.h"
#include "perception/storage_manager.h"
#include "shared_memory_pool.h"
#include "virtual_file_system.h"

using ::perception::Defer;
using ::perception::DirectoryEntry;
using ::perception::FileStatistics;
using ::perception::
    GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest;
using ::perception::ProcessId;
using ::perception::ReadFileRequest;
using ::perception::Status;
using ::perception::StorageManager;
using ::perception::devices::StorageDevice;
using ::perception::devices::StorageDeviceReadRequest;

namespace file_systems {
namespace {

constexpr int kIso9660SectorSize = 2048;
std::string kIso9660Name = "ISO 9660";

class Iso9660File : public File {
 public:
  Iso9660File(StorageDevice::Client storage_device, size_t offset_on_device,
              size_t length_of_file, ProcessId allowed_process)
      : storage_device_(storage_device),
        offset_on_device_(offset_on_device),
        length_of_file_(length_of_file),
        allowed_process_(allowed_process) {};

  virtual Status Close(ProcessId sender) override {
    if (sender != allowed_process_) return ::perception::Status::NOT_ALLOWED;

    Defer([sender, this]() { CloseFile(sender, this); });
    return ::perception::Status::OK;
  }

  virtual Status Read(const ReadFileRequest &request,
                      ProcessId sender) override {
    if (sender != allowed_process_) return ::perception::Status::NOT_ALLOWED;

    if (request.offset_in_file + request.bytes_to_copy > length_of_file_) {
      return ::perception::Status::OVERFLOW;
    }

    StorageDeviceReadRequest read_request;
    read_request.offset_on_device = offset_on_device_ + request.offset_in_file;
    read_request.offset_in_buffer = request.offset_in_destination_buffer;
    read_request.bytes_to_copy = request.bytes_to_copy;
    read_request.buffer = request.buffer_to_copy_into;

    return storage_device_.Read(read_request);
  }

  virtual Status GrantStorageDevicePermissionToAllocateSharedMemoryPages(
      const GrantStorageDevicePermissionToAllocateSharedMemoryPagesRequest
          &request,
      ::perception::ProcessId sender) override {
    if (sender != allowed_process_) return ::perception::Status::NOT_ALLOWED;
    request.buffer->GrantPermissionToLazilyAllocatePage(
        storage_device_.ServerProcessId());

    return Status::OK;
  }

 private:
  StorageDevice::Client storage_device_;
  size_t offset_on_device_;
  size_t length_of_file_;
  ProcessId allowed_process_;
};

void SplitPath(std::string_view path, std::string_view &directory,
               std::string_view &file_name) {
  // Trim off last backslash.
  if (path[path.length() - 1] == '/') path = path.substr(0, path.length() - 1);

  // Find the split point (/) between the mount path and everything else.
  int split_point = path.find_last_of('/');

  if (split_point == std::string_view::npos) {
    directory = "";
    file_name = path;
  } else {
    directory = path.substr(0, split_point);
    file_name = path.substr(split_point + 1);
  }
}

}  // namespace

Iso9660::Iso9660(uint32 size_in_blocks, uint16 logical_block_size,
                 std::unique_ptr<char[]> root_directory,
                 StorageDevice::Client storage_device)
    : size_in_blocks_(size_in_blocks),
      logical_block_size_(logical_block_size),
      root_directory_(std::move(root_directory)),
      FileSystem(storage_device) {}

// Opens a file.
StatusOr<std::unique_ptr<File>> Iso9660::OpenFile(std::string_view path,
                                                  size_t &size_in_bytes,
                                                  ProcessId sender) {
  std::string_view directory, file_name;
  SplitPath(path, directory, file_name);

  std::unique_ptr<File> file;
  ForRawEachEntryInDirectory(
      directory, [&](std::string_view name, DirectoryEntry::Type type,
                     size_t start_lba, size_t size) {
        if (name == file_name) {
          file = std::make_unique<Iso9660File>(
              storage_device_, start_lba * logical_block_size_, size, sender);
          size_in_bytes = size;
          return true;
        }
        return false;
      });

  if (file) {
    return file;
  } else {
    return Status::FILE_NOT_FOUND;
  }
}

// Counts the number of entries in a directory.
size_t Iso9660::CountEntriesInDirectory(std::string_view path) {
  int number_of_entries = 0;
  ForRawEachEntryInDirectory(
      path, [&](std::string_view, DirectoryEntry::Type, size_t, size_t) {
        number_of_entries++;
        return false;
      });
  return number_of_entries;
}

bool Iso9660::ForEachEntryInDirectory(
    std::string_view path, size_t start_index, size_t count,
    const std::function<void(std::string_view, DirectoryEntry::Type, size_t)>
        &on_each_entry) {
  int index = 0;
  bool more_entries_than_we_can_count = false;
  ForRawEachEntryInDirectory(path, [&](std::string_view name,
                                       DirectoryEntry::Type type,
                                       size_t start_lba, size_t size) {
    if (count > 0 && index >= start_index + count) {
      more_entries_than_we_can_count = true;
      return true;
    }
    if (index >= start_index && (index < start_index + count || count == 0)) {
      on_each_entry(name, type, size);
    }
    index++;
    return false;
  });
  return !more_entries_than_we_can_count;
}

void Iso9660::CheckFilePermissions(std::string_view path, bool &file_exists,
                                   bool &can_read, bool &can_write,
                                   bool &can_execute) {
  // Iso9660 is a read-only file system.
  can_write = false;

  if (path.empty()) {
    // Querying the root directory of this file system.
    file_exists = true;
    can_read = true;
    can_execute = true;
    return;
  }

  std::string_view directory, file_name;
  SplitPath(path, directory, file_name);

  file_exists = false;
  ForRawEachEntryInDirectory(
      directory, [&](std::string_view name, DirectoryEntry::Type type,
                     size_t start_lba, size_t size) {
        if (name == file_name) {
          file_exists = true;
          return true;
        }
        return false;
      });
  can_read = file_exists;
  can_execute = file_exists;
}

void Iso9660::ForRawEachEntryInDirectory(
    std::string_view path,
    const std::function<bool(std::string_view, DirectoryEntry::Type, size_t,
                             size_t)> &on_each_entry) {
  auto pooled_shared_memory = kSharedMemoryPool.GetSharedMemory();
  char *buffer = (char *)**pooled_shared_memory->shared_memory;

  StorageDeviceReadRequest read_request;
  read_request.offset_on_device = 0;
  read_request.offset_in_buffer = 0;
  read_request.bytes_to_copy = kIso9660SectorSize;
  read_request.buffer = pooled_shared_memory->shared_memory;

  size_t directory_lba = (size_t)*(uint32 *)&root_directory_[2];
  size_t directory_length = (size_t)*(uint32 *)&root_directory_[10];
  size_t offset = 0;

  // Keep scanning until we enter this directory.
  while (true) {
    // Scan for directory.
    bool found_sub_directory = false;
    int split_index = path.find_first_of('/');
    std::string_view folder_to_find;
    if (split_index == std::string_view::npos) {
      folder_to_find = path;
      path = "";
    } else {
      folder_to_find = path.substr(0, split_index);
      path = path.substr(split_index + 1);

      // Strip out any remaining slashes.
      while (!path.empty() && path[0] == '/') path = path.substr(1);
    }

    // Loop over items in this directory.
    while (directory_length > 0 && !found_sub_directory) {
      // Maybe read in the sector.
      if (offset == 0 || offset + 32 > logical_block_size_) {
        // We need to read in the sector. Note that directory entries aren't
        // allowed to cross sector boundaries.
        size_t directory_start = directory_lba * logical_block_size_;
        read_request.offset_on_device = directory_start;
        auto status = storage_device_.Read(read_request);
        if (status != Status::OK) {
          // Error reading sector.
          kSharedMemoryPool.ReleaseSharedMemory(
              std::move(pooled_shared_memory));
          return;
        }

        // Increment it for the next read.
        directory_lba++;

        // Start reading from the beginning of this new sector.
        offset = 0;
      }

      // Read this record's length.
      size_t record_length = (size_t)*(uint8 *)&buffer[offset] +
                             (size_t)*(uint8 *)&buffer[offset + 1];
      if (record_length <= 0) {
        // End of the sector. We should read the next sector.
        size_t remaining_in_sector = logical_block_size_ - offset;
        if (remaining_in_sector >= directory_length)
          directory_length = 0;
        else
          directory_length -= remaining_in_sector;

        offset = logical_block_size_;
        continue;
      }

      // Read in the entry's name.
      int entry_name_length = (int)*(uint8 *)&buffer[offset + 32];
      std::string_view entry_name =
          std::string_view(&buffer[offset + 33], entry_name_length);

      bool alternative_name = false;

      // See if there is a Rock Ridge name to use instead, which supports
      // up to 255 characters, and is stored as an extension just after
      // the entry name.
      size_t susp_start = entry_name_length + 33;
      if (susp_start % 2 == 1) susp_start++;  // Extensions are 2 byte aligned.

      // Check the system user area (where extensions are).
      while (susp_start + 3 < record_length) {
        char signature_1 = buffer[offset + susp_start];
        char signature_2 = buffer[offset + susp_start + 1];
        size_t extension_length =
            (size_t)*(uint8 *)&buffer[offset + susp_start + 2];
        // We have enough space for Rock Ridge.
        if (signature_1 == 'N' && signature_2 == 'M') {
          // This is a Rock Ridge extension.
          if (susp_start + extension_length <= record_length) {
            // There is space for Rock Ridge extension.
            entry_name = std::string_view(&buffer[offset + susp_start + 5],
                                          extension_length - 5);
            alternative_name = true;
          }
        }
        // Iterate to the next extension.
        susp_start += extension_length;
      }

      if (!alternative_name) {
        // For some reason, entry names are often padded with a non-printable
        // character.
        if (!entry_name.empty() && !std::isprint(entry_name[0]))
          entry_name = entry_name.substr(1);

        // IO 9660 file names have a ';' followed by a revision number.
        // We'll trim this off the end of the file name.
        int semi_colon = entry_name.find_last_of(';');
        if (semi_colon != std::string_view::npos)
          entry_name = entry_name.substr(0, semi_colon);
      }

      if (!entry_name.empty() && entry_name != "." && entry_name != ".." &&
          entry_name != "\1") {
        // Is this a directory?
        bool is_directory = (buffer[offset + 25] & (1 << 1)) == 2;

        size_t entry_start_lba = (size_t)*(uint32 *)&buffer[offset + 2];
        size_t entry_size = (size_t)*(uint32 *)&buffer[offset + 10];

        if (folder_to_find.empty()) {
          if (on_each_entry(entry_name,
                            is_directory ? DirectoryEntry::Type::DIRECTORY
                                         : DirectoryEntry::Type::FILE,
                            entry_start_lba, entry_size)) {
            kSharedMemoryPool.ReleaseSharedMemory(
                std::move(pooled_shared_memory));
            return;
          }
        } else if (folder_to_find == entry_name) {
          found_sub_directory = true;
          directory_lba = entry_start_lba;
          directory_length = entry_size;
          offset = 0;
        }
      }

      if (!found_sub_directory) {
        // Jump to the next record.
        if (record_length < directory_length)
          directory_length -= record_length;
        else
          directory_length = 0;
        offset += record_length;
      }
    }

    if (!found_sub_directory) {
      // There is no subdirectory to enter.
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
      return;
    }
  }
}

std::string_view Iso9660::GetFileSystemType() { return kIso9660Name; }

std::unique_ptr<FileSystem> InitializeIso9960ForStorageDevice(
    StorageDevice::Client storage_device) {
  auto status_or_device_details = storage_device.GetDeviceDetails();
  std::string_view device_name = status_or_device_details->name;

  auto pooled_shared_memory = kSharedMemoryPool.GetSharedMemory();
  char *buffer = (char *)**pooled_shared_memory->shared_memory;

  StorageDeviceReadRequest read_request;
  read_request.offset_on_device = 0;
  read_request.offset_in_buffer = 0;
  read_request.bytes_to_copy = kIso9660SectorSize;
  read_request.buffer = pooled_shared_memory->shared_memory;

  // Start at sector 0x10 and keep looping until we run out of space,
  // stop finding volume descriptors, or find the primary volume descriptor.
  uint64 sector = 0x10;
  while (true) {
    // Read in this sector.
    read_request.offset_on_device = sector * kIso9660SectorSize;
    auto status = storage_device.Read(read_request);
    if (status != Status::OK) {
      // Probably ran past the end of the disk.
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
      return std::unique_ptr<FileSystem>();
    }

    // Are bytes CD001?
    if (buffer[1] != 'C' || buffer[2] != 'D' || buffer[3] != '0' ||
        buffer[4] != '0' || buffer[5] != '1') {
      // No more volume descriptors.
      kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
      return std::unique_ptr<FileSystem>();
    }

    if (buffer[0] == 1) {
      // This is a primary volume descriptor.
      break;
    } else {
      // Jump to the next sector.
      sector++;
    }
  }

  if (buffer[6] != 0x01) {
    std::cout << "Unknown ISO 9660 Version number on " << device_name
              << std::endl;
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
    return std::unique_ptr<FileSystem>();
  }  // Check version

  if (*(uint16 *)&buffer[120] != 1) {
    std::cout << "We only support single set ISO 9660 disks on " << device_name
              << std::endl;
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
    return std::unique_ptr<FileSystem>();
  }

  if (buffer[881] != 0x01) {
    std::cout << "Unsupported ISO 9660 directory records and path table on "
              << device_name << std::endl;
    kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
    return std::unique_ptr<FileSystem>();
  }

  uint32 size_in_blocks = *(uint32 *)&buffer[80];
  uint16 logical_block_size = *(uint16 *)&buffer[128];

  // Copy root directory entry.
  auto root_directory = std::make_unique<char[]>(34);
  memcpy(root_directory.get(), &buffer[156], 34);

  kSharedMemoryPool.ReleaseSharedMemory(std::move(pooled_shared_memory));
  return std::unique_ptr<Iso9660>(
      new Iso9660(size_in_blocks, logical_block_size, std::move(root_directory),
                  storage_device));
}

StatusOr<FileStatistics> Iso9660::GetFileStatistics(std::string_view path) {
  FileStatistics response;
  response.optimal_operation_size = optimal_operation_size_;

  if (path.empty()) return response;

  std::string_view directory, file_name;
  SplitPath(path, directory, file_name);

  ForRawEachEntryInDirectory(
      directory, [&](std::string_view name, DirectoryEntry::Type type,
                     size_t start_lba, size_t size) {
        if (name == file_name) {
          response.exists = true;
          response.type = type;
          response.size_in_bytes = size;
          return true;
        }
        return false;
      });

  return response;
}

}  // namespace file_systems
