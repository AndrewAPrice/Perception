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

#include "audio.h"

#include <iostream>

extern "C" {
#include <SDL_timer.h>

#include "audio/SDL_audio_c.h"
}

namespace {

int OpenDevice(_THIS, const char* devname) {
  std::cout << "TODO: Implement when we have audio support in perception."
            << std::endl;
  _this->hidden = (struct SDL_PrivateAudioData*)0x1;
  return 0;
}

int CaptureFromDevice(_THIS, void* buffer, int buflen) {
  SDL_Delay((_this->spec.samples * 1000) / _this->spec.freq);
  SDL_memset(buffer, _this->spec.silence, buflen);
  return buflen;
}

}  // namespace

extern "C" {

SDL_bool PERCEPTIONAUDIO_Init(SDL_AudioDriverImpl* impl) {
  impl->OpenDevice = OpenDevice;
  impl->CaptureFromDevice = CaptureFromDevice;

  impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
  impl->OnlyHasDefaultCaptureDevice = SDL_TRUE;
  impl->HasCaptureSupport = SDL_TRUE;

  return SDL_TRUE;
}

AudioBootStrap PERCEPTIONAUDIO_bootstrap = {"perception",
                                            "Perception OS Audio Driver",
                                            PERCEPTIONAUDIO_Init, SDL_FALSE};

}  // extern "C"
