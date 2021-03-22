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
#include <iostream>
#include <string>
#include <string_view>

namespace {

void ForEachApplicationFolder(
	const std::function<void(std::string_view)> on_each_application_folder) {
	for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
		for (const auto& application_entry : std::filesystem::directory_iterator(
			std::string(root_entry.path()) + "/Applications")) {
			on_each_application_folder(std::string(application_entry.path()));
		}
	}
}

bool applications_initialized = false;

}

void InitializeApplications() {
	if (applications_initialized)
		return;
	applications_initialized = true;
	ForEachApplicationFolder([](std::string_view application_folder) {
		std::cout << "Possible application in " << application_folder << std::endl;
	});
}
