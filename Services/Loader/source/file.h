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

#pragma once

#include <functional>
#include <string>

#include "perception/memory_span.h"
#include "status.h"

// Represents a file.
class File {
 public:
  virtual ~File() {}

  // Returns a memory span representing the data in the file.
  virtual const ::perception::MemorySpan MemorySpan() const = 0;

  // Returns the name of executable or library that this file belongs to.
  virtual const std::string& Name() const = 0;

  // Returns the path of this file.
  virtual const std::string& Path() const = 0;
};

// Attempts to load a file, returning a unique instance of that file, or an
// empty pointer if the file can't be loaded. Files are not cached or recycled.
// This powers the functions in elf_file_cache.h.
std::unique_ptr<File> LoadFile(std::string_view name);
