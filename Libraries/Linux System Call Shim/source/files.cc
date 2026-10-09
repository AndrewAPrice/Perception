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

#include <errno.h>
#include <fcntl.h>
#include <sys/auxv.h>

#include <algorithm>
#include <map>
#include <mutex>

#include "perception/auxv.h"
#include "perception/debug.h"
#include "perception/services.h"
#include "perception/storage_manager.h"

using ::perception::File;
using ::perception::GetService;
using ::perception::RequestWithFilePath;
using ::perception::StorageManager;

namespace perception {
namespace {

// Minimum file descriptor number allocated by default so standard streams (0-2)
// are not overwritten when a process is launched without attached pipes.
constexpr int kDefaultMinFileDescriptor = 3;

std::mutex files_mutex;
std::string current_working_directory = "/";
std::mutex cwd_mutex;

std::map<long, std::shared_ptr<FileDescriptor>> open_files;
std::map<size_t, std::weak_ptr<SharedMemoryPipe>> pipes_by_shm_id;
bool standard_streams_initialized = false;

std::shared_ptr<SharedMemoryPipe> GetOrCreatePipeForSharedMemoryId(
    size_t shm_id) {
  if (shm_id == 0)
    return nullptr;

  auto itr = pipes_by_shm_id.find(shm_id);
  if (itr != pipes_by_shm_id.end()) {
    if (auto existing = itr->second.lock())
      return existing;
  }

  auto pipe = SharedMemoryPipe::FromSharedMemoryId(shm_id);
  if (!pipe || !pipe->IsValid())
    return nullptr;

  pipes_by_shm_id[shm_id] = pipe;
  return pipe;
}

void EnsureStandardStreamsInitialized() {
  if (standard_streams_initialized)
    return;
  standard_streams_initialized = true;

  size_t stdin_id = getauxval(kAuxvPerceptionStdin);
  if (stdin_id != 0) {
    if (auto pipe = GetOrCreatePipeForSharedMemoryId(stdin_id)) {
      pipe->AddReader();
      auto descriptor = std::make_shared<FileDescriptor>();
      descriptor->type = FileDescriptor::PIPE;
      descriptor->pipe.pipe = pipe;
      descriptor->pipe.is_writer = false;
      descriptor->pipe.non_blocking = false;
      open_files[0] = descriptor;
    }
  }

  size_t stdout_id = getauxval(kAuxvPerceptionStdout);
  if (stdout_id != 0) {
    if (auto pipe = GetOrCreatePipeForSharedMemoryId(stdout_id)) {
      pipe->AddWriter();
      auto descriptor = std::make_shared<FileDescriptor>();
      descriptor->type = FileDescriptor::PIPE;
      descriptor->pipe.pipe = pipe;
      descriptor->pipe.is_writer = true;
      descriptor->pipe.non_blocking = false;
      open_files[1] = descriptor;
    }
  }

  size_t stderr_id = getauxval(kAuxvPerceptionStderr);
  if (stderr_id != 0) {
    if (auto pipe = GetOrCreatePipeForSharedMemoryId(stderr_id)) {
      pipe->AddWriter();
      auto descriptor = std::make_shared<FileDescriptor>();
      descriptor->type = FileDescriptor::PIPE;
      descriptor->pipe.pipe = pipe;
      descriptor->pipe.is_writer = true;
      descriptor->pipe.non_blocking = false;
      open_files[2] = descriptor;
    }
  }
}

int GetFirstUnusedFileDescriptor(int min_fd = kDefaultMinFileDescriptor) {
  int fd = std::max(0, min_fd);
  while (open_files.find(fd) != open_files.end())
    fd++;
  return fd;
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

FileDescriptor::~FileDescriptor() {
  if (closed)
    return;
  closed = true;
  if (type == FileDescriptor::FILE)
    file.file.Close();
  if (type == FileDescriptor::SOCKET)
    socket.socket.Close();
}

long OpenDirectory(const char* path) {
  std::lock_guard<std::mutex> lock(files_mutex);
  EnsureStandardStreamsInitialized();
  long id = GetFirstUnusedFileDescriptor();

  auto descriptor = std::make_shared<FileDescriptor>();
  descriptor->type = FileDescriptor::DIRECTORY;
  descriptor->directory.name = path;
  descriptor->directory.iterating_offset = 0;
  descriptor->directory.finished_iterating = false;

  open_files[id] = descriptor;

  return id;
}

long OpenFile(const char* path, bool read_access, bool write_access,
              bool create_if_not_exists, bool truncate) {
  OpenFileRequest request;
  request.path = path;
  request.read_access = read_access;
  request.write_access = write_access;
  request.create_if_not_exists = create_if_not_exists;
  request.truncate = truncate;

  auto status_or_response = GetService<StorageManager>().OpenFile(request);
  if (!status_or_response) {
    perception::DebugPrinterSingleton
        << "SHIM: OpenFile failed for " << path
        << " status: " << (size_t)status_or_response.Status() << "\n";
    switch (status_or_response.Status()) {
      case Status::FILE_NOT_FOUND:
        return -ENOENT;
      case Status::NOT_ALLOWED:
        return -EACCES;
      default:
        return -EINVAL;
    }
  }

  auto descriptor = std::make_shared<FileDescriptor>();
  descriptor->type = FileDescriptor::FILE;
  descriptor->file.file = status_or_response->file;
  descriptor->file.path = path;
  descriptor->file.size_in_bytes = status_or_response->size_in_bytes;
  descriptor->file.offset_in_file = 0;

  std::lock_guard<std::mutex> lock(files_mutex);
  EnsureStandardStreamsInitialized();
  long id = GetFirstUnusedFileDescriptor();
  open_files[id] = descriptor;
  return id;
}

long CreateSocketDescriptor(perception::network::Socket::Client socket,
                            int domain) {
  auto descriptor = std::make_shared<FileDescriptor>();
  descriptor->type = FileDescriptor::SOCKET;
  descriptor->socket.socket = socket;
  descriptor->socket.domain = domain;

  std::lock_guard<std::mutex> lock(files_mutex);
  EnsureStandardStreamsInitialized();
  long id = GetFirstUnusedFileDescriptor();
  open_files[id] = descriptor;
  return id;
}

int CreatePipeFileDescriptors(int pipefd[2], int flags) {
  if (!pipefd) {
    errno = EFAULT;
    return -EFAULT;
  }
  if ((flags & ~(O_NONBLOCK | O_CLOEXEC)) != 0) {
    errno = EINVAL;
    return -EINVAL;
  }

  auto pipe = SharedMemoryPipe::Create();
  if (!pipe || !pipe->IsValid()) {
    errno = EMFILE;
    return -EMFILE;
  }

  pipe->AddReader();
  pipe->AddWriter();

  bool non_blocking = (flags & O_NONBLOCK) != 0;

  auto read_desc = std::make_shared<FileDescriptor>();
  read_desc->type = FileDescriptor::PIPE;
  read_desc->pipe.pipe = pipe;
  read_desc->pipe.is_writer = false;
  read_desc->pipe.non_blocking = non_blocking;

  auto write_desc = std::make_shared<FileDescriptor>();
  write_desc->type = FileDescriptor::PIPE;
  write_desc->pipe.pipe = pipe;
  write_desc->pipe.is_writer = true;
  write_desc->pipe.non_blocking = non_blocking;

  std::lock_guard<std::mutex> lock(files_mutex);
  EnsureStandardStreamsInitialized();
  if (pipe->GetId() != 0)
    pipes_by_shm_id[pipe->GetId()] = pipe;

  int read_fd = GetFirstUnusedFileDescriptor();
  open_files[read_fd] = read_desc;
  int write_fd = GetFirstUnusedFileDescriptor(read_fd + 1);
  open_files[write_fd] = write_desc;

  pipefd[0] = read_fd;
  pipefd[1] = write_fd;
  return 0;
}

long DuplicateFileDescriptor(int oldfd, int newfd, bool exact_target,
                             int flags) {
  if (oldfd < 0 || newfd < 0) {
    errno = EBADF;
    return -EBADF;
  }
  if ((flags & ~(O_CLOEXEC | O_NONBLOCK)) != 0) {
    errno = EINVAL;
    return -EINVAL;
  }

  std::shared_ptr<FileDescriptor> displaced_descriptor;
  int target_fd = -1;
  {
    std::lock_guard<std::mutex> lock(files_mutex);
    EnsureStandardStreamsInitialized();

    auto old_itr = open_files.find(oldfd);
    if (old_itr == open_files.end()) {
      errno = EBADF;
      return -EBADF;
    }

    if (exact_target && oldfd == newfd)
      return newfd;

    target_fd = exact_target ? newfd : GetFirstUnusedFileDescriptor(newfd);
    if (exact_target) {
      auto existing_itr = open_files.find(target_fd);
      if (existing_itr != open_files.end()) {
        if (existing_itr->second->type == FileDescriptor::PIPE &&
            existing_itr->second->pipe.pipe) {
          if (existing_itr->second->pipe.is_writer)
            existing_itr->second->pipe.pipe->CloseWriter();
          else
            existing_itr->second->pipe.pipe->CloseReader();
        } else {
          displaced_descriptor = existing_itr->second;
        }
        open_files.erase(existing_itr);
      }
    }

    std::shared_ptr<FileDescriptor> new_desc;
    if (old_itr->second->type == FileDescriptor::PIPE) {
      new_desc = std::make_shared<FileDescriptor>(*old_itr->second);
      if ((flags & O_NONBLOCK) != 0)
        new_desc->pipe.non_blocking = true;
      if (new_desc->pipe.pipe) {
        if (new_desc->pipe.is_writer)
          new_desc->pipe.pipe->AddWriter();
        else
          new_desc->pipe.pipe->AddReader();
      }
    } else {
      new_desc = old_itr->second;
    }

    open_files[target_fd] = new_desc;
  }

  if (displaced_descriptor && displaced_descriptor.use_count() == 1 &&
      !displaced_descriptor->closed) {
    displaced_descriptor->closed = true;
    if (displaced_descriptor->type == FileDescriptor::FILE)
      displaced_descriptor->file.file.Close();
    if (displaced_descriptor->type == FileDescriptor::SOCKET)
      displaced_descriptor->socket.socket.Close();
  }

  return target_fd;
}

std::shared_ptr<FileDescriptor> GetFileDescriptor(long id) {
  std::lock_guard<std::mutex> lock(files_mutex);
  EnsureStandardStreamsInitialized();
  auto itr = open_files.find(id);
  if (itr == open_files.end())
    return std::shared_ptr<FileDescriptor>();
  else
    return itr->second;
}

std::shared_ptr<SharedMemoryPipe> GetFileDescriptorPipe(int fd) {
  auto descriptor = GetFileDescriptor(fd);
  if (!descriptor || descriptor->type != FileDescriptor::PIPE)
    return nullptr;
  return descriptor->pipe.pipe;
}

void CloseFile(long id) {
  std::shared_ptr<FileDescriptor> descriptor;
  {
    std::lock_guard<std::mutex> lock(files_mutex);
    EnsureStandardStreamsInitialized();
    auto itr = open_files.find(id);
    if (itr == open_files.end())
      return;
    descriptor = itr->second;
    if (descriptor->type == FileDescriptor::PIPE && descriptor->pipe.pipe) {
      if (descriptor->pipe.is_writer)
        descriptor->pipe.pipe->CloseWriter();
      else
        descriptor->pipe.pipe->CloseReader();
    }
    open_files.erase(itr);
  }

  if (descriptor.use_count() == 1 && !descriptor->closed) {
    descriptor->closed = true;
    if (descriptor->type == FileDescriptor::FILE)
      descriptor->file.file.Close();
    if (descriptor->type == FileDescriptor::SOCKET)
      descriptor->socket.socket.Close();
  }
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

  std::lock_guard<std::mutex> lock(files_mutex);
  memory_mapped_files_by_first_page.insert(
      std::make_pair((size_t)address, mmfile));

  return address;
}

bool MaybeCloseMemoryMappedFile(size_t start_address) {
  std::shared_ptr<MemoryMappedFileEntry> entry;
  {
    std::lock_guard<std::mutex> lock(files_mutex);
    auto itr = memory_mapped_files_by_first_page.find(start_address);
    if (itr == memory_mapped_files_by_first_page.end())
      return false;  // Not a memory mapped file.
    entry = itr->second;
    memory_mapped_files_by_first_page.erase(itr);
  }

  entry->file.Close();
  return true;
}

std::string CurrentWorkingDirectory() {
  std::lock_guard<std::mutex> lock(cwd_mutex);
  return current_working_directory;
}

bool SetCurrentWorkingDirectory(std::string_view cwd) {
  // Clean trailing slashes except if it is "/"
  std::string clean_cwd(cwd);
  while (clean_cwd.length() > 1 && clean_cwd.back() == '/')
    clean_cwd.pop_back();

  // Check if it exists and is a directory
  auto status_or_response =
      GetService<StorageManager>().GetFileStatistics({clean_cwd, false});
  if (!status_or_response || !status_or_response->exists ||
      status_or_response->type !=
          ::perception::DirectoryEntry::Type::DIRECTORY)
    return false;

  std::lock_guard<std::mutex> lock(cwd_mutex);
  current_working_directory = clean_cwd;
  return true;
}

std::string ResolvePath(std::string_view path) {
  if (path.empty())
    return CurrentWorkingDirectory();

  if (path[0] == '/')
    return std::string(path);

  std::string cwd = CurrentWorkingDirectory();
  if (cwd.back() == '/') {
    return cwd + std::string(path);
  } else {
    return cwd + "/" + std::string(path);
  }
}

}  // namespace perception
