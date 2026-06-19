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

#include "settings_loader.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#include "database.h"
#include "nlohmann/json.hpp"

using json = ::nlohmann::json;
using ::perception::RegistryCorpus;
using ::perception::serialization::Value;

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

void LoadSettingsJson(const std::string& path, RegistryCorpus corpus,
                      std::string_view dir_name) {
  std::ifstream file(path);
  if (!file.is_open()) return;

  try {
    json data = json::parse(file);
    std::string ns_name;
    if (data.contains("name") && data["name"].is_string())
      ns_name = data["name"].get<std::string>();

    if (ns_name.empty()) ns_name = std::string(dir_name);

    if (data.contains("groups") && data["groups"].is_array()) {
      for (const auto& gp : data["groups"]) {
        if (gp.contains("settings") && gp["settings"].is_array()) {
          for (const auto& st : gp["settings"]) {
            std::string key;
            if (st.contains("key") && st["key"].is_string())
              key = st["key"].get<std::string>();
            if (key.empty()) continue;

            Value val;
            if (st.contains("default")) val = JsonToValue(st["default"]);

            auto ns = ResolveNamespace(corpus, ns_name, 0);
            if (ns->SetDefaultValue(key, val)) ns->NotifyListeners(key);
          }
        }
      }
    }
  } catch (const std::exception& e) {
    std::cout << "Failed to parse settings schema " << path << ": " << e.what()
              << std::endl;
  }
}

void ScanAndLoadSettingsInSubPath(std::string_view sub_path,
                                  RegistryCorpus corpus) {
  std::error_code ec;
  for (auto const& entry : std::filesystem::directory_iterator(sub_path, ec)) {
    if (entry.is_directory()) {
      std::string settings_file = entry.path().string() + "/settings.json";
      if (std::filesystem::exists(settings_file, ec)) {
        LoadSettingsJson(settings_file, corpus,
                         entry.path().filename().string());
      }
    }
  }
}
}  // namespace

void ScanAndLoadSettings(std::string_view root_path) {
  std::string root_path_str = std::string(root_path);
  ScanAndLoadSettingsInSubPath(root_path_str + "/Applications",
                               RegistryCorpus::APPLICATIONS);
  ScanAndLoadSettingsInSubPath(root_path_str + "/Libraries",
                               RegistryCorpus::LIBRARIES);
}

void ParseRegistryData(std::string_view data) {
  try {
    json root = json::parse(data);
    for (auto& [corpus_str, namespaces] : root.items()) {
      RegistryCorpus corpus;
      if (corpus_str == "applications") {
        corpus = RegistryCorpus::APPLICATIONS;
      } else if (corpus_str == "libraries") {
        corpus = RegistryCorpus::LIBRARIES;
      } else {
        std::cout << "Registry service error: Unknown corpus " << corpus_str
                  << std::endl;
        continue;
      }

      if (!namespaces.is_object()) continue;

      for (auto& [ns_str, keys] : namespaces.items()) {
        if (!keys.is_object()) continue;

        auto ns = ResolveNamespace(corpus, ns_str, 0);

        for (auto& [key_str, json_val] : keys.items()) {
          Value val = JsonToValue(json_val);
          ns->SetValue(key_str, val);
        }
      }
    }
  } catch (const std::exception& e) {
    std::cout << "Failed to parse registry JSON: " << e.what() << std::endl;
  }
}
