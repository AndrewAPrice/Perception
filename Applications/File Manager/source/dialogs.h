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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "perception/ui/node.h"

// Closes the currently active File Manager modal dialog, if any.
void CloseActiveDialog();

// Shows a single-line text input dialog for creating or renaming an item.
void ShowTextInputDialog(std::string_view title, std::string_view prompt,
                         std::string_view initial_value,
                         std::string_view confirm_label,
                         std::shared_ptr<perception::ui::Node> parent_window,
                         std::function<void(std::string)> on_confirm,
                         std::function<void()> on_closed = nullptr);

// Shows a confirmation dialog for deleting one or more filesystem paths.
void ShowDeleteConfirmationDialog(
    const std::vector<std::string>& targets,
    std::shared_ptr<perception::ui::Node> parent_window,
    std::function<void()> on_confirm,
    std::function<void()> on_closed = nullptr);
