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
#include <string_view>

#include "perception/disk/disk_manager.h"
#include "perception/ui/node.h"

namespace dialogs {

// Shows dialog to change the mount path of a mounted volume.
void ShowChangeMountPathDialog(
    perception::disk::DiskManager& disk_manager,
    std::string_view current_mount_point,
    std::shared_ptr<::perception::ui::Node> parent_window = nullptr,
    std::function<void()> on_mount_path_changed = nullptr);

}  // namespace dialogs
