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

#include "types.h"

struct Process;

#define MODULE_NAME_WORDS 11
#define MODULE_NAME_LENGTH (MODULE_NAME_WORDS * 8)

// Parses the name of the multiboot module, which has the permissions of the
// process before the title, e.g. "abc Some Program" is a process titled "Some
// Program" with the permissions a, b, and c.
bool ParseMultibootModuleName(char** name, size_t* name_length, bool* is_driver,
                              bool* can_create_processes);

// Load the modules provided by the multiboot boot loader.
void LoadMultibootModules();

// Attempts to load a multiboot module into a process. Each subsequential call
// will load the next multiboot module into a process. When there are no more to
// load, `size` will be 0. All parameters other than `process` are output
// parameters. The lowest 16 bits of `address_and_flags` is a bit field.
// The first process that a multiboot module loads into becomes the only process
// that a multiboot module can be loaded into.
void LoadNextMultibootModuleIntoProcess(Process* process,
                                        size_t& address_and_flags, size_t& size,
                                        char* name);

// Whether there are still remaining unloaded multiboot modules.
bool HasRemainingUnloadedMultibootModules();
