// Copyright 2025 Google LLC
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
#include <string_view>

#include "perception/file.h"
#include "perception/memory_mapped_file.h"
#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"

namespace perception {
namespace serialization {
class Serializer;
}

class DirectoryEntry : public serialization::Serializable {
 public:
  std::string name;

  enum class Type { FILE, DIRECTORY };
  Type type;

  uint64 size_in_bytes;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class RequestWithFilePath : public serialization::Serializable {
 public:
  RequestWithFilePath() {}
  RequestWithFilePath(std::string_view path) : path(path) {}
  std::string path;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class OpenFileResponse : public serialization::Serializable {
 public:
  File::Client file;
  uint64 size_in_bytes;
  uint64 optimal_operation_size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class OpenMemoryMappedFileResponse : public serialization::Serializable {
 public:
  MemoryMappedFile::Client file;
  std::shared_ptr<SharedMemory> file_contents;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class ReadDirectoryRequest : public serialization::Serializable {
 public:
  std::string path;
  uint64 first_index;
  uint64 maximum_number_of_entries;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class ReadDirectoryResponse : public serialization::Serializable {
 public:
  std::vector<DirectoryEntry> entries;
  bool has_more_entries;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class CheckPermissionsResponse : public serialization::Serializable {
 public:
  bool exists;
  bool can_read;
  bool can_write;
  bool can_execute;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class FileStatistics : public serialization::Serializable {
 public:
  bool exists;
  DirectoryEntry::Type type;
  uint64 size_in_bytes;
  uint64 optimal_operation_size;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                                         \
  X(1, OpenFile, OpenFileResponse, RequestWithFilePath)                        \
  X(2, OpenMemoryMappedFile, OpenMemoryMappedFileResponse,                     \
    RequestWithFilePath)                                                       \
  X(3, ReadDirectory, ReadDirectoryResponse, ReadDirectoryRequest)             \
  X(4, CheckPermissions, CheckPermissionsResponse, RequestWithFilePath) \
  X(5, GetFileStatistics, FileStatistics, RequestWithFilePath)

DEFINE_PERCEPTION_SERVICE(StorageManager, "perception.StorageManager",
                          METHOD_LIST)

#undef METHOD_LIST

}  // namespace perception