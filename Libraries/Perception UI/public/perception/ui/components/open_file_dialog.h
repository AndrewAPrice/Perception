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
#include <string>
#include <string_view>
#include <vector>

namespace perception {
namespace ui {
namespace components {

// Shows an dialog where the user can select which file to open.
//
// Parameters:
// - on_open_file: Lambda called when a file is selected or the dialog is
// closed/cancelled.
//                 The first parameter is true if a file was successfully
//                 selected, and the second parameter contains the full
//                 filesystem path.
// - extensions_to_filter: Optional vector of file extensions (e.g. {"png",
// "jpg"}).
//                         If non-empty, only files matching these extensions
//                         (case-insensitively) will be visible/selectable.
//                         Directories are always shown for navigation. Leading
//                         dots in extensions are handled automatically.
// - default_directory: Optional directory path to open initially. If empty, "/"
// is used
//                      for the first dialog instance, and the parent directory
//                      of the last successfully opened file in subsequent
//                      instances.
// - title: Optional window title for the dialog (e.g. "Open Image").
void ShowOpenFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_open_file,
    const std::vector<std::string>& extensions_to_filter = {},
    std::string_view default_directory = "",
    std::string_view title = "Open File");

}  // namespace components
}  // namespace ui
}  // namespace perception
