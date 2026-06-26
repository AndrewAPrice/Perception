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

#include <iostream>
#include "testing.h"
#include "file_systems/ramdisk.h"
#include "file_systems/overlay.h"
#include "perception/shared_memory.h"

using ::file_systems::RamdiskFileSystem;
using ::file_systems::OverlayFileSystem;
using ::file_systems::RamdiskFileContent;
using ::perception::ProcessId;
using ::Status;
using ::perception::SharedMemory;
using ::perception::ReadFileRequest;
using ::perception::WriteFileRequest;

TEST(RamdiskFileOperations) {
  RamdiskFileSystem fs;

  size_t size;
  ProcessId sender = 123;

  // 1. Create file and write content
  {
    auto file_or = fs.OpenFile("test.txt", size, sender, true, true, true, false);
    ASSERT(true, file_or.Ok());
    auto file = std::move(*file_or);

    // Write content
    auto write_sm = SharedMemory::FromSize(4096, 0);
    ASSERT(true, (bool)write_sm);
    std::string data_to_write = "Hello, Ramdisk!";
    std::memcpy(**write_sm, data_to_write.data(), data_to_write.length());

    WriteFileRequest write_req;
    write_req.offset_in_file = 0;
    write_req.bytes_to_copy = data_to_write.length();
    write_req.buffer_to_copy_from = write_sm;

    Status status = file->Write(write_req, sender);
    EXPECT(Status::OK, status);

    // Close file
    status = file->Close(sender);
    EXPECT(Status::OK, status);
  }

  // 2. Open and read back content
  {
    auto file_read_or = fs.OpenFile("test.txt", size, sender, true, false, false, false);
    ASSERT(true, file_read_or.Ok());
    auto file_read = std::move(*file_read_or);
    std::string data_to_write = "Hello, Ramdisk!";
    EXPECT(data_to_write.length(), size);

    auto read_sm = SharedMemory::FromSize(4096, 0);
    ASSERT(true, (bool)read_sm);

    ReadFileRequest read_req;
    read_req.offset_in_file = 0;
    read_req.offset_in_destination_buffer = 0;
    read_req.bytes_to_copy = size;
    read_req.buffer_to_copy_into = read_sm;

    Status status = file_read->Read(read_req, sender);
    EXPECT(Status::OK, status);

    std::string data_read((char*)**read_sm, size);
    EXPECT(data_to_write, data_read);

    // Close read file
    status = file_read->Close(sender);
    EXPECT(Status::OK, status);
  }
}

TEST(RamdiskDirectoryAndDeletion) {
  RamdiskFileSystem fs;
  ProcessId sender = 123;

  // Verify initial instances
  int initial_instances = RamdiskFileContent::GetInstances();

  // 1. Create directories
  Status status = fs.CreateDirectory("dir1", sender);
  EXPECT(Status::OK, status);

  status = fs.CreateDirectory("dir1/dir2", sender);
  EXPECT(Status::OK, status);

  // 2. Create nested files inside a block so their unique_ptrs are destroyed
  {
    size_t size;
    auto file1_or = fs.OpenFile("dir1/file1.txt", size, sender, true, true, true, false);
    ASSERT(true, file1_or.Ok());
    EXPECT(Status::OK, (*file1_or)->Close(sender));

    auto file2_or = fs.OpenFile("dir1/dir2/file2.txt", size, sender, true, true, true, false);
    ASSERT(true, file2_or.Ok());
    EXPECT(Status::OK, (*file2_or)->Close(sender));
  }

  // Should have 2 file content instances now
  EXPECT(initial_instances + 2, RamdiskFileContent::GetInstances());

  // 3. Delete non-empty directory dir1 (should recursively delete dir1/file1.txt, dir1/dir2, and dir1/dir2/file2.txt)
  status = fs.DeleteFileOrDirectory("dir1", sender);
  EXPECT(Status::OK, status);

  // Everything should be fully cleaned up!
  EXPECT(initial_instances, RamdiskFileContent::GetInstances());
}

TEST(OverlayCoWAndTombstones) {
  auto base = std::make_unique<RamdiskFileSystem>();
  ProcessId sender = 123;

  // Seed base with a file
  std::vector<uint8_t> base_content = {'B', 'a', 's', 'e'};
  base->CreateFileWithContent("base_file.txt", base_content);

  OverlayFileSystem overlay(std::move(base));

  // Verify file exists and has correct content
  {
    size_t size;
    auto file_or = overlay.OpenFile("base_file.txt", size, sender, true, false, false, false);
    ASSERT(true, file_or.Ok());
    EXPECT(size_t(4), size);

    auto read_sm = SharedMemory::FromSize(4096, 0);
    ASSERT(true, (bool)read_sm);

    ReadFileRequest read_req;
    read_req.offset_in_file = 0;
    read_req.offset_in_destination_buffer = 0;
    read_req.bytes_to_copy = size;
    read_req.buffer_to_copy_into = read_sm;

    EXPECT(Status::OK, (*file_or)->Read(read_req, sender));
    std::string read_str((char*)**read_sm, size);
    EXPECT(std::string("Base"), read_str);
    EXPECT(Status::OK, (*file_or)->Close(sender));
  }

  // Modify file (triggers CoW)
  {
    size_t size;
    auto file_write_or = overlay.OpenFile("base_file.txt", size, sender, true, true, false, false);
    ASSERT(true, file_write_or.Ok());

    auto write_sm = SharedMemory::FromSize(4096, 0);
    ASSERT(true, (bool)write_sm);
    std::string mod_data = "Modified";
    std::memcpy(**write_sm, mod_data.data(), mod_data.length());

    WriteFileRequest write_req;
    write_req.offset_in_file = 0;
    write_req.bytes_to_copy = mod_data.length();
    write_req.buffer_to_copy_from = write_sm;

    EXPECT(Status::OK, (*file_write_or)->Write(write_req, sender));
    EXPECT(Status::OK, (*file_write_or)->Close(sender));
  }

  // Read back modified content
  {
    size_t size;
    auto file_read_or = overlay.OpenFile("base_file.txt", size, sender, true, false, false, false);
    ASSERT(true, file_read_or.Ok());
    std::string mod_data = "Modified";
    EXPECT(mod_data.length(), size);

    auto read_sm = SharedMemory::FromSize(4096, 0);
    ASSERT(true, (bool)read_sm);

    ReadFileRequest read_req;
    read_req.offset_in_file = 0;
    read_req.offset_in_destination_buffer = 0;
    read_req.bytes_to_copy = size;
    read_req.buffer_to_copy_into = read_sm;

    EXPECT(Status::OK, (*file_read_or)->Read(read_req, sender));
    std::string read_mod_str((char*)**read_sm, size);
    EXPECT(mod_data, read_mod_str);
    EXPECT(Status::OK, (*file_read_or)->Close(sender));
  }

  // Delete base_file.txt (tombstones it)
  EXPECT(Status::OK, overlay.DeleteFileOrDirectory("base_file.txt", sender));

  // Opening should now fail with FILE_NOT_FOUND
  size_t size;
  auto file_fail_or = overlay.OpenFile("base_file.txt", size, sender, true, false, false, false);
  EXPECT(Status::FILE_NOT_FOUND, file_fail_or.Status());
}
