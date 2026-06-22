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

#include "schema.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

#include "nlohmann/json.hpp"
#include "perception/services.h"
#include "perception/storage_manager.h"

using ::nlohmann::json;
using ::perception::RegistryCorpus;
using ::perception::serialization::Value;

std::map<std::string, ActiveSetting> all_settings;
std::vector<PackageMetadata> all_packages;
std::string selected_page_path = "";
int selected_package_index = 0;

const std::vector<std::string>& GetMountPoints() {
  static std::vector<std::string> mount_points = []() {
    auto status_or_response =
        ::perception::GetService<::perception::StorageManager>()
            .GetMountedFileSystems();
    if (status_or_response) return status_or_response->mount_points;
    return std::vector<std::string>{};
  }();
  return mount_points;
}

const std::vector<std::string>& GetInstalledApplications() {
  static std::vector<std::string> installed_apps = []() {
    std::set<std::string> apps;

    auto scan_dir = [&](const std::string& dir_path) {
      std::error_code ec;
      if (!std::filesystem::exists(dir_path, ec)) return;
      for (const auto& entry :
           std::filesystem::directory_iterator(dir_path, ec)) {
        if (entry.is_directory()) {
          std::string name = entry.path().filename().string();
          if (!name.empty()) apps.insert(name);
        }
      }
    };

    scan_dir("/Applications");
    scan_dir("/Libraries");

    for (const auto& mount : GetMountPoints()) {
      scan_dir("/" + mount + "/Applications");
      scan_dir("/" + mount + "/Libraries");
    }

    return std::vector<std::string>(apps.begin(), apps.end());
  }();
  return installed_apps;
}

namespace {

Value JsonToValue(const json& j) {
  if (j.is_boolean()) {
    return Value(j.get<bool>());
  } else if (j.is_number_integer()) {
    return Value(j.get<int64>());
  } else if (j.is_number_float()) {
    return Value(j.get<double>());
  } else if (j.is_string()) {
    return Value(j.get<std::string>());
  } else if (j.is_object()) {
    if (j.contains("r") && j.contains("g") && j.contains("b")) {
      int r = j["r"].get<int>();
      int g = j["g"].get<int>();
      int b = j["b"].get<int>();
      return Value(static_cast<uint32>((0xFF << 24) | ((r & 0xFF) << 16) |
                                       ((g & 0xFF) << 8) | (b & 0xFF)));
    }
  } else if (j.is_array()) {
    std::vector<Value> arr;
    for (const auto& item : j) arr.push_back(JsonToValue(item));
    return Value(arr);
  }
  return Value();
}

void LoadSchemaFile(const std::string& path, RegistryCorpus corpus,
                    std::string_view dir_name) {
  std::ifstream file(path);
  if (!file.is_open()) return;

  try {
    json data = json::parse(file);
    std::string ns_name;
    if (data.contains("name") && data["name"].is_string())
      ns_name = data["name"].get<std::string>();
    if (ns_name.empty()) ns_name = std::string(dir_name);

    bool found_pkg = false;
    for (const auto& pkg : all_packages) {
      if (pkg.corpus == corpus && pkg.ns_name == ns_name) {
        found_pkg = true;
        break;
      }
    }
    if (!found_pkg) {
      all_packages.push_back({corpus, ns_name, ns_name});
    }

    auto parse_setting = [&](const json& st, const std::string& fallback_page) {
      ActiveSetting ast;
      ast.corpus = corpus;
      ast.ns_name = ns_name;
      if (st.contains("key") && st["key"].is_string())
        ast.key = st["key"].get<std::string>();
      if (ast.key.empty()) return;

      if (st.contains("name") && st["name"].is_string()) {
        ast.name = st["name"].get<std::string>();
      } else {
        ast.name = ast.key;
      }

      std::string raw_type_str;
      if (st.contains("type")) {
        if (st["type"].is_string()) {
          raw_type_str = st["type"].get<std::string>();
        } else if (st["type"].is_array()) {
          raw_type_str = "table";
          for (const auto& col_json : st["type"]) {
            TableColumn col;
            if (col_json.contains("name") && col_json["name"].is_string())
              col.name = col_json["name"].get<std::string>();
            std::string col_type_str;
            if (col_json.contains("type") && col_json["type"].is_string())
              col_type_str = col_json["type"].get<std::string>();
            col.type = ParseSettingType(col_type_str);
            ast.table_columns.push_back(col);
          }
        }
      }
      if (st.contains("description") && st["description"].is_string()) {
        ast.description = st["description"].get<std::string>();
      }
      if (st.contains("default")) {
        ast.default_val = JsonToValue(st["default"]);
      }
      if (st.contains("options") && st["options"].is_array()) {
        for (const auto& opt : st["options"]) {
          if (opt.is_string()) ast.options.push_back(opt.get<std::string>());
        }
      }
      ast.type = ParseSettingType(raw_type_str, !ast.options.empty());
      std::string page_str;
      if (st.contains("page") && st["page"].is_string()) {
        page_str = st["page"].get<std::string>();
      } else {
        page_str = fallback_page;
      }
      for (char& c : page_str) {
        if (c == '/') c = '>';
      }
      ast.page = page_str;

      std::string change_key =
          (corpus == RegistryCorpus::APPLICATIONS ? "a:" : "l:") + ns_name +
          ":" + ast.key;
      all_settings[change_key] = ast;
    };

    if (data.contains("settings") && data["settings"].is_array()) {
      for (const auto& st : data["settings"])
        parse_setting(st, "Applications>" + ns_name);
    }
    if (data.contains("groups") && data["groups"].is_array()) {
      for (const auto& gp : data["groups"]) {
        std::string gp_title;
        if (gp.contains("title") && gp["title"].is_string())
          gp_title = gp["title"].get<std::string>();
        std::string fallback = "Applications>" + ns_name;
        if (!gp_title.empty()) fallback += ">" + gp_title;
        if (gp.contains("settings") && gp["settings"].is_array()) {
          for (const auto& st : gp["settings"]) parse_setting(st, fallback);
        }
      }
    }
  } catch (const std::exception& e) {
    std::cout << "Settings app error: Failed to parse settings schema " << path
              << ": " << e.what() << std::endl;
  }
}

void ScanDirectoryForSettings(const std::string& dir_path,
                              RegistryCorpus corpus) {
  std::error_code ec;
  if (!std::filesystem::exists(dir_path, ec)) return;

  for (const auto& entry : std::filesystem::directory_iterator(dir_path, ec)) {
    if (entry.is_directory()) {
      std::string settings_file = entry.path().string() + "/settings.json";
      if (std::filesystem::exists(settings_file, ec)) {
        LoadSchemaFile(settings_file, corpus, entry.path().filename().string());
      }
    }
  }
}

}  // namespace

void ScanAllSettings() {
  all_settings.clear();
  all_packages.clear();
  ScanDirectoryForSettings("/Applications", RegistryCorpus::APPLICATIONS);
  ScanDirectoryForSettings("/Libraries", RegistryCorpus::LIBRARIES);

  for (const auto& mount : GetMountPoints()) {
    ScanDirectoryForSettings("/" + mount + "/Applications",
                             RegistryCorpus::APPLICATIONS);
    ScanDirectoryForSettings("/" + mount + "/Libraries",
                             RegistryCorpus::LIBRARIES);
  }

  std::sort(all_packages.begin(), all_packages.end(),
            [](const PackageMetadata& a, const PackageMetadata& b) {
              std::string a_lower = a.ns_name;
              std::string b_lower = b.ns_name;
              std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(),
                             ::tolower);
              std::transform(b_lower.begin(), b_lower.end(), b_lower.begin(),
                             ::tolower);
              if (a_lower != b_lower) return a_lower < b_lower;
              return a.corpus < b.corpus;
            });

  for (size_t i = 0; i < all_packages.size(); ++i) {
    bool collision = false;
    for (size_t j = 0; j < all_packages.size(); ++j) {
      if (i != j && all_packages[i].ns_name == all_packages[j].ns_name) {
        collision = true;
        break;
      }
    }
    if (collision) {
      all_packages[i].display_name =
          all_packages[i].ns_name +
          (all_packages[i].corpus == RegistryCorpus::APPLICATIONS
               ? " (Application)"
               : " (Library)");
    } else {
      all_packages[i].display_name = all_packages[i].ns_name;
    }
  }
}

SettingType ParseSettingType(std::string_view type_str, bool has_options) {
  if (has_options) return SettingType::OPTIONS;
  static const std::map<std::string, SettingType, std::less<>> kTypeMap = {
      {"table", SettingType::TABLE},
      {"bool", SettingType::BOOLEAN},
      {"boolean", SettingType::BOOLEAN},
      {"int", SettingType::INTEGER},
      {"integer", SettingType::INTEGER},
      {"float", SettingType::FLOAT},
      {"Color", SettingType::COLOR},
      {"color", SettingType::COLOR},
      {"permission", SettingType::PERMISSION},
      {"application", SettingType::APPLICATION},
  };
  auto it = kTypeMap.find(type_str);
  if (it != kTypeMap.end()) return it->second;
  return SettingType::STRING;
}

Value GetDefaultValueForColumnType(SettingType type) {
  switch (type) {
    case SettingType::BOOLEAN:
      return Value(false);
    case SettingType::PERMISSION:
      return Value("CanReadAllFiles");
    case SettingType::APPLICATION:
      return Value("Storage Manager");
    default:
      return Value("");
  }
}
