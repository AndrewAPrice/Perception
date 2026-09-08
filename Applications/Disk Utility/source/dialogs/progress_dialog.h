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

#include <memory>
#include <string_view>

#include "perception/ui/node.h"

namespace dialogs {

// Shows a modal progress dialog during long disk operations.
std::shared_ptr<::perception::ui::Node> ShowProgressDialog(
    std::string_view title, std::string_view message,
    std::shared_ptr<::perception::ui::Node> parent_window = nullptr);

// Closes a modal progress dialog previously opened with ShowProgressDialog.
void CloseProgressDialog(
    std::shared_ptr<::perception::ui::Node> progress_dialog);

}  // namespace dialogs
