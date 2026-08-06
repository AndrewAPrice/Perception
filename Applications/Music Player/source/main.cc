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

#include <string_view>

#include "music_player_window.h"
#include "perception/scheduler.h"

using ::perception::HandOverControl;

int main(int argc, char* argv[]) {
  std::string_view initial_file_path;
  if (argc > 1 && argv[1] != nullptr) initial_file_path = argv[1];

  MusicPlayerWindow window(initial_file_path);

  HandOverControl();
  return 0;
}
