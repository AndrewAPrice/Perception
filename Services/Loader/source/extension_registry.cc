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

}  // namespace

std::optional<std::reference_wrapper<const ExtensionCommand>> GetCommandForPath(
    std::string_view path) {
  if (path.empty()) {
    return std::nullopt;
  }

  if (path.back() == '/') {
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
