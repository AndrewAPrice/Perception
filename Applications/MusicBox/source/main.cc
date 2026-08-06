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

#include <cstdlib>
#include <string_view>

#include "perception/processes.h"
#include "perception/random.h"
#include "perception/scheduler.h"
#include "synth_engine.h"
#include "windows/music_box_window.h"

using ::perception::HandOverControl;

int main(int argc, char* argv[]) {
  std::srand(perception::RandomNumber());

  InitializeSynthEngine();

  std::string_view initial_song_path;
  if (argc > 1 && argv[1] != nullptr) initial_song_path = argv[1];

  windows::MusicBoxWindow window(initial_song_path);

  HandOverControl();
  return 0;
}
