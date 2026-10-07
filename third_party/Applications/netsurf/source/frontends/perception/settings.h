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
#include <utility>

struct nsoption_s;

namespace netsurf {
namespace perception {

// Sets the default system colors in the options table.
void SetSystemColorDefaults(struct nsoption_s* defaults);

// Loads settings from the registry into NetSurf options and registers live
// listeners for updates.
void LoadSettingsFromRegistry();

// Registers live listeners for settings updates from the registry.
void RegisterSettingsListeners();

// Returns whether browsing history, cookies, and bookmarks should be persisted
// to disk.
bool IsStoragePersistenceEnabled();

// Returns the configured search engine URL template (with optional %s
// placeholder).
const std::string& GetSearchUrlTemplate();

// Returns the configured search engine URL prefix.
const std::string& GetSearchUrl();

// Builds a navigable URL or search query URL from address bar input.
std::string BuildNavigationUrlFromInput(std::string_view input);

// Builds a navigable URL or search query URL from address bar input.
template <typename T = std::string_view>
inline std::string BuildNavigationUrl(T&& input) {
  return BuildNavigationUrlFromInput(std::string_view(std::forward<T>(input)));
}

}  // namespace perception
}  // namespace netsurf
