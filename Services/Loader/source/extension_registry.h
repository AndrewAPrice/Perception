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

#include <functional>
#include <optional>
#include <string>
#include <string_view>

enum class ExtensionAction {
  // This is an application that can be executed immediately.
  EXECUTE,
  // Call another application to handle this.
  CALL_APPLICATION
};

// What to do with the given path.
struct ExtensionCommand {
  // The type of action to take.
  ExtensionAction action;

  // The application that handles this file type. This is only used if action
  // is CALL_APPLICATION.
  std::string application;
};

// Returns the command associated with a given path (file or directory).
// Returns nullopt if no command is registered for the given path.
std::optional<std::reference_wrapper<const ExtensionCommand>> GetCommandForPath(
    std::string_view path);
