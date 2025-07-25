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

#include <memory>
#include <mutex>

#include "file.h"
#include "perception/shared_memory.h"
#include "perception/storage_manager.h"

class MemoryMappedFile : public ::perception::MemoryMappedFile::Server {
 public:
  MemoryMappedFile(std::unique_ptr<File> file, size_t length_of_file,
                   size_t optimal_operation_size,
                   ::perception::ProcessId allowed_process);

  virtual ::perception::Status Close(::perception::ProcessId sender) override;

  std::shared_ptr<::perception::SharedMemory> GetBuffer();

 private:
  std::unique_ptr<File> file_;
  ::perception::ProcessId allowed_process_;
  size_t length_of_file_;
  std::shared_ptr<::perception::SharedMemory> buffer_;
  std::mutex mutex_;

  // The optimal size of operations, in bytes.
  size_t optimal_operation_size_;

  // Should this file close after all operations?
  bool close_after_all_operations_;

  // Is the file closed?
  bool is_closed_;

  // How many operations are running?
  int running_operations_;

  // Reads in a page-sized chunk of the file into the buffer.
  void ReadInPageChunk(size_t start_of_page);

  // Maybe closes the file if there are no operations running.
  void MaybeCloseIfUnlocked();

  // Closes the file.
  void CloseFile();
};