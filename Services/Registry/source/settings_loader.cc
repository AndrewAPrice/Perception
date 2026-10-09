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

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

#include "database.h"
#include "nlohmann/json.hpp"
#include "perception/services.h"
#include "perception/storage_manager.h"
#include "perception/time.h"

using json = ::nlohmann::json;
using ::perception::RegistryCorpus;
using ::perception::StorageManager;
using ::perception::serialization::Value;

namespace {

// Idle duration in seconds before modified registry values are flushed to disk.
constexpr int kIdleFlushDelaySeconds = 10;

std::mutex flush_mutex;
bool is_dirty = false;
uint64 modification_generation = 0;

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

json ValueToJson(const Value& val) {
  switch (val.GetType()) {
    case Value::Type::BOOLEAN:
      return json(val.BoolValue().value_or(false));
    case Value::Type::INTEGER:
      return json(val.IntegerValue().value_or(0));
    case Value::Type::FLOAT:
      return json(val.FloatValue().value_or(0.0));
    case Value::Type::STRING:
      return json(std::string(val.StringValue().value_or("")));
    case Value::Type::COLOR_RGB: {
      uint32 color = val.ColorRGBValue().value_or(0);
      return json{{"r", static_cast<int>((color >> 16) & 0xFF)},
                  {"g", static_cast<int>((color >> 8) & 0xFF)},
                  {"b", static_cast<int>(color & 0xFF)}};
    }
    case Value::Type::ARRAY: {
      json arr = json::array();
      if (const auto* vec = val.ArrayValue()) {
        for (const auto& item : *vec) arr.push_back(ValueToJson(item));
      }
      return arr;
    }
    case Value::Type::UNDEFINED:
    default:
      return json();
  }
}

std::string SerializeDatabaseToJson() {
  json root = json::object();
  json apps = json::object();
  json libs = json::object();

  for (const auto& ns : GetAllNamespaceObjects()) {
    auto persistent_values = ns->GetPersistentValues();
    if (persistent_values.empty()) continue;

    json ns_obj = json::object();
    for (const auto& [key, val] : persistent_values)
      ns_obj[key] = ValueToJson(val);

    if (ns->GetCorpus() == RegistryCorpus::APPLICATIONS)
      apps[std::string(ns->GetName())] = std::move(ns_obj);
    else if (ns->GetCorpus() == RegistryCorpus::LIBRARIES)
      libs[std::string(ns->GetName())] = std::move(ns_obj);
  }

  if (!apps.empty()) root["applications"] = std::move(apps);
  if (!libs.empty()) root["libraries"] = std::move(libs);
  return root.dump(2);
}

void LoadSettingEntry(const json& st,
                      const std::shared_ptr<RegistryNamespace>& ns,
                      std::string_view instance_prefix) {
  std::string key;
  if (st.contains("key") && st["key"].is_string())
    key = st["key"].get<std::string>();
  if (key.empty()) return;

  bool is_read_only = st.contains("readonly") && st["readonly"].is_boolean() &&
                      st["readonly"].get<bool>();
  if (!instance_prefix.empty()) {
    if (is_read_only) ns->MarkReadOnlyInstancePattern(instance_prefix, key);
    return;
  }

  Value val;
  if (st.contains("default")) val = JsonToValue(st["default"]);

  if (st.contains("options") && st["options"].is_array() &&
      val.GetType() == Value::Type::STRING) {
    std::string default_str = std::string(val.StringValue().value_or(""));
    for (const auto& opt : st["options"]) {
      if (opt.is_object() && opt.contains("name") &&
          opt["name"].is_string() &&
          opt["name"].get<std::string>() == default_str &&
          opt.contains("value")) {
        val = JsonToValue(opt["value"]);
        break;
      }
    }
  }

  if (is_read_only) ns->MarkReadOnly(key);
  if (ns->SetDefaultValue(key, val)) ns->NotifyListeners(key);
}

void LoadSettingsJson(const std::string& path, RegistryCorpus corpus,
                      std::string_view dir_name) {
  std::ifstream file(path);
  if (!file.is_open()) return;

  try {
    json data = json::parse(file);
    std::vector<json> pages;
    if (data.is_array()) {
      for (const auto& item : data) {
        if (item.is_object()) pages.push_back(item);
      }
    } else if (data.is_object()) {
      pages.push_back(data);
    }

    for (const auto& page_obj : pages) {
      std::string ns_name;
      if (page_obj.contains("name") && page_obj["name"].is_string())
        ns_name = page_obj["name"].get<std::string>();

      if (ns_name.empty()) ns_name = std::string(dir_name);
      auto ns = ResolveNamespace(corpus, ns_name, 0);

      if (page_obj.contains("settings") && page_obj["settings"].is_array()) {
        for (const auto& st : page_obj["settings"])
          LoadSettingEntry(st, ns, "");
      }

      if (page_obj.contains("groups") && page_obj["groups"].is_array()) {
        for (const auto& gp : page_obj["groups"]) {
          std::string instance_prefix;
          if (gp.contains("instancePrefix") &&
              gp["instancePrefix"].is_string()) {
            instance_prefix = gp["instancePrefix"].get<std::string>();
          } else if (gp.contains("keyPrefix") && gp["keyPrefix"].is_string()) {
            instance_prefix = gp["keyPrefix"].get<std::string>();
          }

          if (gp.contains("settings") && gp["settings"].is_array()) {
            for (const auto& st : gp["settings"])
              LoadSettingEntry(st, ns, instance_prefix);
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

void RecordRegistryModification() {
  uint64 captured_gen = 0;
  {
    std::scoped_lock lock(flush_mutex);
    is_dirty = true;
    captured_gen = ++modification_generation;
  }

  ::perception::AfterDuration(
      std::chrono::seconds(kIdleFlushDelaySeconds), [captured_gen]() {
        bool should_flush = false;
        {
          std::scoped_lock lock(flush_mutex);
          if (is_dirty && modification_generation == captured_gen)
            should_flush = true;
        }
        if (should_flush) FlushRegistryToDisk();
      });
}

void FlushRegistryToDisk() {
  uint64 flush_gen = 0;
  {
    std::scoped_lock lock(flush_mutex);
    if (!is_dirty) return;
    flush_gen = modification_generation;
  }

  auto storage_manager =
      ::perception::FindFirstInstanceOfService<StorageManager>();
  if (!storage_manager) return;

  auto mounts_or = storage_manager->GetMountedFileSystems();
  if (!mounts_or.Ok()) return;

  std::vector<std::string> target_paths;
  std::vector<std::string> fallback_paths;
  std::error_code ec;
  for (const auto& fs : mounts_or->file_systems) {
    if (!fs.is_writable) continue;
    std::string candidate = "/" + fs.mount_point + "/registry.json";
    if (fs.is_boot_drive || std::filesystem::exists(candidate, ec))
      target_paths.push_back(candidate);
    else
      fallback_paths.push_back(candidate);
  }
  if (target_paths.empty()) target_paths = std::move(fallback_paths);
  if (target_paths.empty()) return;

  std::string serialized = SerializeDatabaseToJson();
  bool wrote_any = false;
  for (const auto& path : target_paths) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (out.is_open()) {
      out << serialized;
      out.flush();
      if (out.good()) wrote_any = true;
    }
  }

  if (wrote_any) {
    std::scoped_lock lock(flush_mutex);
    if (modification_generation == flush_gen) is_dirty = false;
  }
}
