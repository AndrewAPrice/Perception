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

#include "perception/audio.h"

#include "perception/devices/audio_device.h"
#include "perception/services.h"

namespace perception {

AudioStreamID PlayAudio(std::shared_ptr<SharedMemory> buffer, float volume,
                        bool loop, uint32 sample_rate, uint8 channels,
                        uint8 bits_per_sample) {
  devices::AudioDevicePlayRequest request;
  request.shared_buffer = buffer;
  request.volume = volume;
  request.loop = loop;
  request.sample_rate = sample_rate;
  request.channels = channels;
  request.bits_per_sample = bits_per_sample;

  auto response_or =
      GetService<devices::AudioDevice>().PlayAudio(request);
  if (!response_or) return 0;
  return response_or->stream_id;
}

void StopAudio(AudioStreamID stream_id) {
  devices::AudioDeviceStopRequest request;
  request.stream_id = stream_id;
  (void)GetService<devices::AudioDevice>().StopAudio(request, nullptr);
}

void StopAllAudio() {
  (void)GetService<devices::AudioDevice>().StopAllAudio(nullptr);
}

void SetAudioVolume(float volume) {
  devices::AudioDeviceSetVolumeRequest request;
  request.volume = volume;
  (void)GetService<devices::AudioDevice>().SetVolume(request, nullptr);
}

float GetAudioVolume() {
  auto response_or = GetService<devices::AudioDevice>().GetVolume();
  if (!response_or) return 1.0f;
  return response_or->volume;
}

}  // namespace perception
