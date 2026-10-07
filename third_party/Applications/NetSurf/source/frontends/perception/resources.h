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

#include <cstddef>
#include <string>
#include <vector>

struct browser_window;
struct hlcache_handle;

namespace netsurf::perception {

// Classification categories for resources loaded by a web page.
enum class ResourceCategory {
  kDocument,
  kStylesheet,
  kScript,
  kImage,
  kBackgroundImage,
  kObject,
  kFrame,
};

// Metadata and content handle for a single page resource.
struct PageResource {
  ResourceCategory category = ResourceCategory::kDocument;
  std::string name;
  std::string url;
  std::string mime_type;
  size_t size_bytes = 0;
  std::string status;
  int width = 0;
  int height = 0;
  struct hlcache_handle* handle = nullptr;
  std::string inline_source;
  bool is_internal = false;
};

// Returns a display label for a resource category.
const char* ResourceCategoryToString(ResourceCategory category);

// Walks the active content of a browser window and collects all page resources.
std::vector<PageResource> CollectPageResources(struct browser_window* bw);

// Overload accepting a browser_window reference.
inline std::vector<PageResource> CollectPageResources(
    struct browser_window& bw) {
  return CollectPageResources(&bw);
}

// Alias for CollectPageResources accepting a pointer.
inline std::vector<PageResource> CollectResources(struct browser_window* bw) {
  return CollectPageResources(bw);
}

// Alias for CollectPageResources accepting a reference.
inline std::vector<PageResource> CollectResources(struct browser_window& bw) {
  return CollectPageResources(&bw);
}

}  // namespace netsurf::perception

namespace resources {

using ::netsurf::perception::CollectPageResources;
using ::netsurf::perception::CollectResources;
using ::netsurf::perception::PageResource;
using ::netsurf::perception::ResourceCategory;
using ::netsurf::perception::ResourceCategoryToString;

}  // namespace resources

using ::netsurf::perception::CollectPageResources;
using ::netsurf::perception::CollectResources;
using ::netsurf::perception::PageResource;
using ::netsurf::perception::ResourceCategory;
using ::netsurf::perception::ResourceCategoryToString;
