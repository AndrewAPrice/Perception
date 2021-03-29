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

#include "applications.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void MaybeLoadApplication(std::string_view path) {
	std::cout << "Maybe load " << path << std::endl;
	std::ifstream launcher_metadata_file(std::string(path) + "/launcher.json");
	if (!launcher_metadata_file.is_open()) {
		std::cout << "Unable to open launcher.json" << std::endl;
		return;
	}
	char c = launcher_metadata_file.get();
	while (launcher_metadata_file.good()) {
	    std::cout << c;
	    c = launcher_metadata_file.get();
  	}
  	launcher_metadata_file.close();
}

void LoadApplications() {
	for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
		for (const auto& application_entry : std::filesystem::directory_iterator(
			std::string(root_entry.path()) + "/Applications")) {
			MaybeLoadApplication(std::string(application_entry.path()));
		}
	}
}

bool applications_initialized = false;

}

void InitializeApplications() {
	if (applications_initialized)
		return;
	applications_initialized = true;
	LoadApplications();
}
