// Copyright 2026
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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/node.h"

namespace ui_debugger {

std::shared_ptr<::perception::ui::Node> BuildEnumDropdown(
    std::string_view label_name,
    const std::vector<std::string>& options,
    std::string_view current_val,
    std::function<void(std::string_view)> on_change,
    bool editable = true);

std::shared_ptr<::perception::ui::Node> BuildSingleInput(
    std::string_view label_name,
    std::string_view current_val,
    std::function<void(std::string_view)> on_change,
    bool editable = true);

std::shared_ptr<::perception::ui::Node> BuildPairInput(
    std::string_view label_name,
    std::string_view val1,
    std::string_view val2,
    std::function<void(std::string_view)> on_change1,
    std::function<void(std::string_view)> on_change2,
    bool editable = true);

std::shared_ptr<::perception::ui::Node> BuildSectionHeader(
    std::string_view title);

}  // namespace ui_debugger
