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
#include <vector>

#include "nlohmann/json.hpp"

using json = ::nlohmann::json;

namespace {

std::vector<Application> applications;

void MaybeLoadApplication(std::string_view path) {
  try {
    std::ifstream launcher_metadata_file(std::string(path) + "/launcher.json");
    if (!launcher_metadata_file.is_open()) {
      // The application is missing a launcher.json so we won't show it in the
      // launcher.
      return;
    }
    json data = json::parse(launcher_metadata_file);
    Application application;
    auto name_itr = data.find("name");
    if (name_itr != data.end() && name_itr->is_string())
      name_itr->get_to(application.name);

    if (application.name.empty())
      application.name = std::filesystem::path(path).filename();

    application.path = std::string(path) + "/" +
                       std::string(std::filesystem::path(path).filename()) +
                       ".app";
    auto description_itr = data.find("description");
    if (description_itr != data.end() && description_itr->is_string())
      description_itr->get_to(application.description);

    applications.push_back(application);

    launcher_metadata_file.close();
  } catch (...) {
    std::cout << "Unhandled exception" << std::endl;
  }
}

bool applications_initialized = false;

}  // namespace

void ScanForApplications() {
  if (applications_initialized) return;
  applications_initialized = true;

  for (const auto& root_entry : std::filesystem::directory_iterator("/")) {
    for (const auto& application_entry : std::filesystem::directory_iterator(
             std::string(root_entry.path()) + "/Applications")) {
      MaybeLoadApplication(std::string(application_entry.path()));
    }
  }
}

const std::vector<Application>& GetApplications() {
  return applications;
}
