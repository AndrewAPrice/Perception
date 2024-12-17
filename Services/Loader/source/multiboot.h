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

#include <string_view>

#include "perception/multiboot.h"

using ::perception::MultibootModule;

// Loads an ELF executables and libraries from the multiboot modules.
void LoadMultibootModules();

// Returns a multiboot module with the given name. Returns nullptr if no module
// is found. Also only returns a module once, subsequent calls for `name` will
// return nullptr.
std::unique_ptr<::perception::MultibootModule> GetMultibootModule(
    std::string_view name);
