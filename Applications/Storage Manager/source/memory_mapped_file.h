// Copyright 2022 Google LLC
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

#include "permebuf/Libraries/perception/storage_manager.permebuf.h"
#include "perception/shared_memory.h"
#include "file.h"

class MemoryMappedFile : public ::permebuf::perception::MemoryMappedFile::Server {
 public:
  typedef ::permebuf::perception::MemoryMappedFile MMF;

  MemoryMappedFile(std::unique_ptr<File> file, size_t length_of_file,
                   ::perception::ProcessId allowed_process);

  virtual void HandleCloseFile(::perception::ProcessId sender,
                               const MMF::CloseFileMessage&) override;

  ::perception::SharedMemory& GetBuffer();

 private:
  std::unique_ptr<File> file_;
  ::perception::ProcessId allowed_process_;
  size_t length_of_file_;
  std::unique_ptr<::perception::SharedMemory> buffer_;
  std::mutex mutex_;
  size_t cycles_to_read_chunks_;
  size_t cycles_for_mutex_;
  size_t cycles_for_checking_if_page_is_allocated_;
  size_t cycles_for_allocating_temp_buffer_;
  size_t cycles_for_building_read_file_request_;
  size_t cycles_for_read_file_rpc_;
  size_t cycles_to_assign_page_;
  size_t cycles_to_allocate_page_;
  size_t cycles_to_copy_page_;
  size_t cycles_to_release_shared_memory_;

  // Reads in a page-sized chunk of the file into the buffer.
  void ReadInPageChunk(size_t start_of_page);
};