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

#pragma once

#include <memory>
#include <string>

#include "perception/memory_span.h"
#include "types.h"

namespace perception {

// Gets the details of the framebuffer set up by the bootloader.
void GetMultibootFramebufferDetails(size_t& physical_address, uint32& width,
                                    uint32& height, uint32& pitch, uint8& bpp);

// Details about a multiboot module.
struct MultibootModule {
  // The name of this module.
  std::string name;

  // The data for this module.
  MemorySpan data;

  // A bit field of flags for this module.
  size_t flags;

  // Whether this module is a driver.
  bool IsDriver() { return flags & 1; }

  // Whether this module can launch processes.
  bool CanLaunchProcesses() { return flags & 2; }
};

// Returns a multiboot module from the kernel. Only the first process that calls
// this function can make subsequent calls to it. The memory from the module is
// mapped into the process.
std::unique_ptr<MultibootModule> GetMultibootModule();

}  // namespace perception
