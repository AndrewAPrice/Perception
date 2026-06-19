// Copyright 2024 Google LLC
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

#include <memory>
#include <string_view>
#include <vector>

#include "perception/ui/node.h"

// The different tabs of the launcher.
enum class Tab { APPLICATIONS, PROCESSES };

// Returns the label of a tab.
std::string_view GetTabLabel(Tab tab);

// Gets or constructs the the contents for a tab.
std::shared_ptr<::perception::ui::Node> GetOrConstructTabContents(Tab tab);
