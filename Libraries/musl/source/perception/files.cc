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

#include "perception/files.h"

#include <map>

namespace perception {
namespace {

std::map<long, std::shared_ptr<FileDescriptor>> open_files;

long last_file_id = 0;

long GetUniqueFileId() {
	last_file_id++;
	return last_file_id;
}

}

long OpenDirectory(const char* path) {
	long id = GetUniqueFileId();

	auto descriptor = std::make_shared<FileDescriptor>();
	descriptor->type = FileDescriptor::DIRECTORY;
	descriptor->directory.name = path;
	descriptor->directory.iterating_offset = 0;

	open_files[id] = descriptor;

	return id;
}

std::shared_ptr<FileDescriptor> GetFileDescriptor(long id) {
	auto itr = open_files.find(id);
	if (itr == open_files.end())
		return std::shared_ptr<FileDescriptor>();
	else
		return itr->second;
}

void CloseFile(long id) {
	auto itr = open_files.find(id);
	if (itr == open_files.end())
		return;

	open_files.erase(itr);
}

}