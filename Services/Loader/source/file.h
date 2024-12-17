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

class File {
    public:
        virtual ~File() {}
        virtual const ::perception::MemorySpan MemorySpan() const = 0;
        virtual const std::string& Name() const = 0;
        virtual const std::string& Path() const = 0;
};

std::unique_ptr<File> GetExecutableFile(std::string_view name);
