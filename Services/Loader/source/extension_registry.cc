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

#include "extension_registry.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

#include "multiboot.h"
#include "perception/registry.h"

namespace {

// The command to run if the path is a directory.
const ExtensionCommand kDirectoryCommand = {ExtensionAction::CALL_APPLICATION,
                                            "File Manager"};

std::string GetExtension(std::string_view path) {
  std::string_view trimmed_path = path;
  if (!trimmed_path.empty() && trimmed_path.back() == '/') {
    trimmed_path.remove_suffix(1);
  }
  std::string ext = std::filesystem::path(trimmed_path).extension().string();
  if (!ext.empty() && ext[0] == '.') {
    ext.erase(0, 1);
  }
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  return ext;
}

std::optional<ExtensionCommand> GetCommandFromRegistry(
    std::string_view extension) {
  auto val_or = perception::GetRegistryValue(
      perception::RegistryCorpus::APPLICATIONS, "Loader", "associations");
  if (!val_or.Ok()) {
    return std::nullopt;
  }

  const auto* arr = (*val_or).ArrayValue();
  if (!arr) return std::nullopt;

  for (const auto& item : *arr) {
    const auto* entry = item.ArrayValue();
    if (!entry || entry->size() < 2) continue;

    auto entry_ext = (*entry)[0].StringValue();
    auto entry_app = (*entry)[1].StringValue();

    if (entry_ext && entry_app && *entry_ext == extension) {
      ExtensionCommand cmd;
      if (*entry_ext == "app" || *entry_ext == "") {
        cmd.action = ExtensionAction::EXECUTE;
      } else {
        cmd.action = ExtensionAction::CALL_APPLICATION;
      }
      cmd.application = std::string(*entry_app);
      return cmd;
    }
  }

  return std::nullopt;
}

}  // namespace

std::optional<ExtensionCommand> GetCommandForPath(std::string_view path) {
  if (path.empty()) return std::nullopt;

  if (IsLoadingMultibootModules())
    return (ExtensionCommand){ExtensionAction::EXECUTE, ""};

  std::string ext = GetExtension(path);
  bool is_app = (ext == "app");
  bool is_dir = (path.back() == '/');

  // Determine query extension key
  std::string query_ext = ext;
  if (is_app) {
    query_ext = "app";
  } else if (is_dir) {
    query_ext = "dir";
  }

  auto reg_cmd = GetCommandFromRegistry(query_ext);
  if (reg_cmd.has_value()) return reg_cmd;

  // Fallback to trying to execute it.
  return (ExtensionCommand){ExtensionAction::EXECUTE, ""};
}
