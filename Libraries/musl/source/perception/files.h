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

#include <string>

namespace perception {

struct FileDescriptor {
	enum Type {
		DIRECTORY = 0
	};
	Type type;

	struct Directory {
		std::string name;
		int iterating_offset;
		bool finished_iterating;
	} directory;
};

long OpenDirectory(const char* path);

std::shared_ptr<FileDescriptor> GetFileDescriptor(long id);

void CloseFile(long id);

}