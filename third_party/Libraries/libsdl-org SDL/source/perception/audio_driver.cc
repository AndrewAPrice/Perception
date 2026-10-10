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

#include "audio_driver.h"

#include <cstring>
#include <iostream>

#include "perception/audio.h"

extern "C" {
#include <SDL_timer.h>

#include "audio/SDL_audio_c.h"
}

namespace {

int OpenDevice(_THIS, const char* devname) {
  auto data = new SDL_PrivateAudioData();
  data->buffer = ::perception::SharedMemory::FromSize(_this->spec.size, 0);
  if (!data->buffer || **data->buffer == nullptr) {
    delete data;
    return -1;
  }
  data->stream_id = ::perception::PlayAudio(
      data->buffer, 1.0f, true, _this->spec.freq, _this->spec.channels, 16);
  _this->hidden = data;
  return 0;
}

void PlayDevice(_THIS) {
  if (!_this || !_this->hidden || !_this->work_buffer || _this->spec.size == 0)
    return;
  auto data = _this->hidden;
  if (data->buffer && **data->buffer != nullptr) {
    std::memcpy(**data->buffer, _this->work_buffer, _this->spec.size);
  }
}

void WaitDevice(_THIS) {
  if (_this->spec.freq > 0 && _this->spec.samples > 0) {
    SDL_Delay((_this->spec.samples * 1000) / _this->spec.freq);
  }
}

void CloseDevice(_THIS) {
  if (_this && _this->hidden) {
    auto data = _this->hidden;
    if (data->stream_id != 0) {
      ::perception::StopAudio(data->stream_id);
    }
    delete data;
    _this->hidden = nullptr;
  }
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
  impl->PlayDevice = PlayDevice;
  impl->WaitDevice = WaitDevice;
  impl->CloseDevice = CloseDevice;
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
