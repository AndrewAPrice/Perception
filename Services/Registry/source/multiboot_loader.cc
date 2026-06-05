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

#include "multiboot_loader.h"

#include <iostream>
#include <string_view>

#include "perception/loader.h"
#include "perception/services.h"
#include "settings_loader.h"

void LoadMultibootRegistry() {
  auto loader = ::perception::GetService<::perception::Loader>();
  if (loader.IsValid()) {
    ::perception::GetMultibootRegistryFileRequest req;
    auto res_or = loader.GetMultibootRegistryFile(req);
    if (res_or.Ok()) {
      const char* data = res_or->registry_file.ToSpan().ToType<const char>();
      size_t length = res_or->registry_file.ToSpan().Length();
      std::cout << "Registry: Loaded multiboot registry.json (" << length
                << " bytes)" << std::endl;
      ParseRegistryData(std::string_view(data, length));
    } else {
      std::cout << "Registry: No multiboot registry.json module found."
                << std::endl;
    }
  }
}
