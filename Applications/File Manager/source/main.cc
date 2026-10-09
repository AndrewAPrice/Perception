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

#include <filesystem>
#include <string>
#include <system_error>

#include "file_manager_window.h"
#include "perception/scheduler.h"

namespace {

// Default directory opened when no valid directory argument is provided.
constexpr std::string_view kDefaultStartingDirectory = "/";

}  // namespace

int main(int argc, char* argv[]) {
  std::string starting_directory(kDefaultStartingDirectory);
  if (argc > 1) {
    std::string arg = argv[1];
    if (arg.size() > 1 && arg.back() == '/') arg.pop_back();
    std::error_code ec;
    if (std::filesystem::is_directory(arg, ec)) starting_directory = arg;
  }

  FileManagerWindow window(starting_directory);
  window.Initialize();

  perception::HandOverControl();
  return 0;
}
