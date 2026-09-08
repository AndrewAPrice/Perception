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

namespace perception {
namespace ui {

class Node;

namespace components {

// Mode for the file dialog.
enum class FileDialogType {
  Open,
  Save,
};

// Options for configuring ShowFileDialog.
struct FileDialogOptions {
  // Whether this is an Open or Save dialog.
  FileDialogType type = FileDialogType::Open;

  // Callback called when a file is selected or chosen (succeeded = true) or
  // closed/cancelled (succeeded = false).
  std::function<void(bool succeeded, std::string_view path)> on_complete;

  // Optional vector of file extensions (e.g. {"png", "jpg"}).
  // If non-empty, only files matching these extensions (case-insensitively) will
  // be visible in the list. Leading dots are handled automatically. In Save
  // mode, if a user specifies a filename without an extension, the first
  // extension in this list is appended.
  std::vector<std::string> extensions_to_filter = {};

  // Optional initial file path or filename (used in Save mode).
  std::string_view default_file_path = "";

  // Optional directory path to open initially. If empty, "/" is used for the
  // first instance and the parent directory of the last chosen file in
  // subsequent instances.
  std::string_view default_directory = "";

  // Optional window title for the dialog. If empty, a default title based on
  // the dialog type is used ("Open File" or "Save File").
  std::string_view title = "";

  // Optional parent window node. If provided, the parent window will be
  // disabled while this dialog is open.
  std::shared_ptr<Node> parent_window = nullptr;
};

// Shows a dialog where the user can select an existing file to open or specify
// a file path to save.
void ShowFileDialog(const FileDialogOptions& options);

// Shows a dialog where the user can select which file to open.
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
// - parent_window: Optional parent window node. If provided, the parent window
//                  will be disabled while this dialog is open.
void ShowOpenFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_open_file,
    const std::vector<std::string>& extensions_to_filter = {},
    std::string_view default_directory = "",
    std::string_view title = "Open File",
    std::shared_ptr<Node> parent_window = nullptr);

// Shows a dialog where the user can choose a destination file to save.
//
// Parameters:
// - on_save_file: Lambda called when a file path is chosen or the dialog is
// closed/cancelled.
//                 The first parameter is true if a file path was successfully
//                 chosen, and the second parameter contains the full
//                 filesystem path.
// - extensions_to_filter: Optional vector of file extensions (e.g. {"song"}).
//                         If non-empty, files in the directory are filtered to
//                         these extensions, and if the user enters a filename
//                         without an extension, the first extension in this list
//                         will be automatically appended.
// - default_file_path: Optional initial file path or filename (e.g.
//                      "/Applications/MusicBox/songs/Untitled Song.song" or
//                      "Untitled Song.song").
// - default_directory: Optional directory path to open initially. If empty and
//                      default_file_path contains a directory, that directory
//                      is used.
// - title: Optional window title for the dialog (e.g. "Save File").
// - parent_window: Optional parent window node. If provided, the parent window
//                  will be disabled while this dialog is open.
void ShowSaveFileDialog(
    std::function<void(bool succeeded, std::string_view path)> on_save_file,
    const std::vector<std::string>& extensions_to_filter = {},
    std::string_view default_file_path = "",
    std::string_view default_directory = "",
    std::string_view title = "Save File",
    std::shared_ptr<Node> parent_window = nullptr);

}  // namespace components
}  // namespace ui
}  // namespace perception
