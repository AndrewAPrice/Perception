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
#include <map>
#include <string>

#include "multiboot.h"
#include "perception/registry.h"

namespace {

// The map of file extensions and what should handle it.
const std::map<std::string, ExtensionCommand, std::less<>> kExtensionMap = {
    {"jpg", {ExtensionAction::CALL_APPLICATION, "Viewer"}},
    {"png", {ExtensionAction::CALL_APPLICATION, "Viewer"}},
    {"jpeg", {ExtensionAction::CALL_APPLICATION, "Viewer"}},
    {"svg", {ExtensionAction::CALL_APPLICATION, "Viewer"}},
    {"rgba", {ExtensionAction::CALL_APPLICATION, "Viewer"}},
    {"app", {ExtensionAction::EXECUTE, ""}},
    {"", {ExtensionAction::EXECUTE, ""}}};

// The command to run if the path is a directory.
const ExtensionCommand kDirectoryCommand = {ExtensionAction::CALL_APPLICATION,
                                            "File Manager"};

std::optional<ExtensionCommand> GetStaticCommand(bool is_app, bool is_dir,
                                                 std::string_view path) {
  if (is_app) {
    auto it = kExtensionMap.find("app");
    if (it != kExtensionMap.end()) {
      return it->second;
    }
  }

  if (is_dir) {
    return kDirectoryCommand;
  }

  std::string_view ext = "";
  auto dot_idx = path.find_last_of('.');
  auto slash_idx = path.find_last_of('/');
  if (dot_idx != std::string_view::npos &&
      (slash_idx == std::string_view::npos || dot_idx > slash_idx)) {
    ext = path.substr(dot_idx + 1);
  }

  std::string ext_lower = std::string(ext);
  std::transform(ext_lower.begin(), ext_lower.end(), ext_lower.begin(),
                 ::tolower);

  auto it = kExtensionMap.find(ext_lower);
  if (it != kExtensionMap.end()) {
    return it->second;
  }

  return std::nullopt;
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
  if (path.empty()) {
    return std::nullopt;
  }

  std::string_view trimmed_path = path;
  if (trimmed_path.back() == '/') {
    trimmed_path.remove_suffix(1);
  }

  bool is_app = false;
  if (trimmed_path.size() >= 4 &&
      trimmed_path.substr(trimmed_path.size() - 4) == ".app") {
    is_app = true;
  }

  bool is_dir = (path.back() == '/');

  if (IsLoadingMultibootModules()) {
    return GetStaticCommand(is_app, is_dir, path);
  }

  // Determine query extension key
  std::string ext = "";
  if (is_app) {
    ext = "app";
  } else if (is_dir) {
    ext = "dir";
  } else {
    std::string_view path_ext = "";
    auto dot_idx = path.find_last_of('.');
    auto slash_idx = path.find_last_of('/');
    if (dot_idx != std::string_view::npos &&
        (slash_idx == std::string_view::npos || dot_idx > slash_idx)) {
      path_ext = path.substr(dot_idx + 1);
    }
    ext = std::string(path_ext);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  }

  auto reg_cmd = GetCommandFromRegistry(ext);
  if (reg_cmd.has_value()) {
    return reg_cmd;
  }

  // Fall back to static compile-time commands
  return GetStaticCommand(is_app, is_dir, path);
}
