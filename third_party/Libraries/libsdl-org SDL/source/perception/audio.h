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

extern "C" {
#include "SDL_audio.h"
#include "SDL_internal.h"
#include "audio/SDL_sysaudio.h"
}

#define _THIS SDL_AudioDevice *_this

struct SDL_PrivateAudioData {
  int dummy;
};

#ifdef __cplusplus
extern "C" {
#endif

SDL_bool PERCEPTIONAUDIO_Init(SDL_AudioDriverImpl* impl);

#ifdef __cplusplus
}
#endif
