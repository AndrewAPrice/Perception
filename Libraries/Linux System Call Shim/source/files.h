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

#pragma once

#include <string>

#include "perception/file.h"
#include "perception/memory_mapped_file.h"
#include "perception/shared_memory.h"
#include "perception/shared_memory_pool.h"

namespace perception {

extern SharedMemoryPool<kPageSize> kSharedMemoryPool;

struct FileDescriptor {
  enum Type { DIRECTORY = 0, FILE = 1 };
  Type type;

  struct Directory {
    std::string name;
    int iterating_offset;
    bool finished_iterating;
  } directory;

  struct File {
    perception::File::Client file;
    std::string path;
    size_t size_in_bytes;
    size_t offset_in_file;
  } file;
};

long OpenDirectory(const char* path);
long OpenFile(const char* path);

std::shared_ptr<FileDescriptor> GetFileDescriptor(long id);
bool ReadAndIncrementFile(long id, void* buffer, long bytes);

void CloseFile(long id);

void* AddMemoryMappedFile(::perception::MemoryMappedFile::Client file,
                          std::shared_ptr<::perception::SharedMemory> buffer);

bool MaybeCloseMemoryMappedFile(size_t start_address);

}  // namespace perception