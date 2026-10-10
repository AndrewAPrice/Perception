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

#include <string>
#include <string_view>
#include <vector>

// Metadata describing a built-in slash command, global function, namespace
// member, or pipeline method for autocomplete and inline signature hints.
struct ApiEntry {
  std::string name;
  std::string signature;
  std::string description;
  std::string return_type;
  bool is_function = true;
};

// Returns the catalog of all supported REPL slash commands.
const std::vector<ApiEntry>& GetSlashCommandCatalog();

// Returns the catalog of top-level global functions, variables, and namespaces.
const std::vector<ApiEntry>& GetGlobalCatalog();

// Returns the catalog of members for a built-in namespace (e.g. "fs",
// "fs.path", "proc", "pipe", "sys", "registry", "clipboard", "net", "term",
// "term.style").
const std::vector<ApiEntry>& GetNamespaceCatalog(std::string_view ns_name);

// Returns the catalog of chainable methods on Command / Pipeline instances.
const std::vector<ApiEntry>& GetCommandMethodCatalog();

// Looks up a specific API entry by receiver namespace/type ("", "slash",
// "Command", "fs", "proc", etc.) and member name. Returns nullptr if not found.
const ApiEntry* FindApiEntry(std::string_view receiver, std::string_view name);
