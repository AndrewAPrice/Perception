// Copyright 2026 Google LLC
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

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "nlohmann/json.hpp"
#include "perception/registry.h"

enum class SettingType {
  STRING,
  INTEGER,
  FLOAT,
  BOOLEAN,
  COLOR,
  OPTIONS,
  TABLE,
  PERMISSION,
  APPLICATION
};

struct TableColumn {
  std::string name;
  SettingType type;
};

struct ActiveSetting {
  ::perception::RegistryCorpus corpus;
  std::string ns_name;
  std::string key;
  std::string name;
  SettingType type;
  std::string description;
  std::vector<std::string> options;
  ::perception::serialization::Value default_val;
  std::vector<TableColumn> table_columns;
  std::string page;
};

struct PackageMetadata {
  ::perception::RegistryCorpus corpus;
  std::string ns_name;
  std::string display_name;
};

extern std::map<std::string, ActiveSetting> all_settings;
extern std::vector<PackageMetadata> all_packages;
extern std::string selected_page_path;
extern int selected_package_index;

const std::vector<std::string>& GetInstalledApplications();
void ScanAllSettings();

SettingType ParseSettingType(std::string_view type_str, bool has_options = false);
::perception::serialization::Value GetDefaultValueForColumnType(SettingType type);
