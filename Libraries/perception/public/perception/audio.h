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

#include <memory>

#include "perception/shared_memory.h"
#include "types.h"

namespace perception {

typedef uint64 AudioStreamID;

AudioStreamID PlayAudio(std::shared_ptr<SharedMemory> buffer,
                        float volume = 1.0f, bool loop = false,
                        uint32 sample_rate = 48000, uint8 channels = 2,
                        uint8 bits_per_sample = 16);

void StopAudio(AudioStreamID stream_id);

void StopAllAudio();

void SetAudioVolume(float volume);

float GetAudioVolume();

}  // namespace perception
