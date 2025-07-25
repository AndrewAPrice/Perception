// Copyright 2020 Google LLC
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

#include "files.h"

#include <map>

#include "perception/services.h"
#include "perception/storage_manager.h"

using ::perception::File;
using ::perception::GetService;
using ::perception::RequestWithFilePath;
using ::perception::StorageManager;

namespace perception {
namespace {

std::map<long, std::shared_ptr<FileDescriptor>> open_files;

long last_file_id = 0;

long GetUniqueFileId() {
  last_file_id++;
  return last_file_id;
}

struct MemoryMappedFileEntry {
  ::perception::MemoryMappedFile::Client file;
  std::shared_ptr<::perception::SharedMemory> buffer;
  size_t first_page, last_page;
};
std::map<size_t, std::shared_ptr<MemoryMappedFileEntry>>
    memory_mapped_files_by_first_page;

}  // namespace

SharedMemoryPool<kPageSize> kSharedMemoryPool;

long OpenDirectory(const char* path) {
  long id = GetUniqueFileId();

  auto descriptor = std::make_shared<FileDescriptor>();
  descriptor->type = FileDescriptor::DIRECTORY;
  descriptor->directory.name = path;
  descriptor->directory.iterating_offset = 0;
  descriptor->directory.finished_iterating = false;

  open_files[id] = descriptor;

  return id;
}

long OpenFile(const char* path) {
  auto status_or_response = GetService<StorageManager>().OpenFile({path});
  if (!status_or_response) {
    return -1;
  }

  auto descriptor = std::make_shared<FileDescriptor>();
  descriptor->type = FileDescriptor::FILE;
  descriptor->file.file = status_or_response->file;
  descriptor->file.path = path;
  descriptor->file.size_in_bytes = status_or_response->size_in_bytes;
  descriptor->file.offset_in_file = 0;

  long id = GetUniqueFileId();
  open_files[id] = descriptor;
  return id;
}

std::shared_ptr<FileDescriptor> GetFileDescriptor(long id) {
  auto itr = open_files.find(id);
  if (itr == open_files.end())
    return std::shared_ptr<FileDescriptor>();
  else
    return itr->second;
}

void CloseFile(long id) {
  auto itr = open_files.find(id);
  if (itr == open_files.end()) return;

  if (itr->second->type == FileDescriptor::FILE) itr->second->file.file.Close();

  open_files.erase(itr);
}

void* AddMemoryMappedFile(::perception::MemoryMappedFile::Client file,
                          std::shared_ptr<::perception::SharedMemory> buffer) {
  void* address = **buffer;
  size_t size = buffer->GetSize();

  auto mmfile = std::make_shared<MemoryMappedFileEntry>();
  mmfile->file = file;
  mmfile->buffer = buffer;
  mmfile->first_page = (size_t)address;
  mmfile->last_page = ((size_t)address + size) & ~(kPageSize - 1);
  memory_mapped_files_by_first_page.insert(
      std::make_pair((size_t)address, mmfile));

  return address;
}

bool MaybeCloseMemoryMappedFile(size_t start_address) {
  auto itr = memory_mapped_files_by_first_page.find(start_address);
  if (itr == memory_mapped_files_by_first_page.end())
    return false;  // Not a memory mapped file.

  itr->second->file.Close();
  memory_mapped_files_by_first_page.erase(itr);
  return true;
}

}  // namespace perception