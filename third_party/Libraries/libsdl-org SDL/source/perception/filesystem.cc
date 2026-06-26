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

extern "C" {
#include "SDL_filesystem.h"
#include "SDL_error.h"
#include "SDL_stdinc.h"
}

#include <string>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {

char *SDL_GetBasePath(void) {
  return SDL_strdup("/");
}

char *SDL_GetPrefPath(const char *org, const char *app) {
  if (!app) {
    SDL_InvalidParamError("app");
    return nullptr;
  }
  std::string path = "/Applications/";
  path += app;
  path += "/data/";

  // Ensure directory exists
  mkdir(path.c_str(), 0777);

  return SDL_strdup(path.c_str());
}

}
