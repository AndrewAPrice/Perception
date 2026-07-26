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

#include "perception/audio_manager.h"

#include "perception/serialization/serializer.h"
#include "perception/services.h"

namespace perception {

void AudioPlayRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Shared buffer", shared_buffer);
  serializer.Integer("Loop", loop);
  serializer.Float("Volume", volume);
  serializer.Integer("Sample rate", sample_rate);
  serializer.Integer("Channels", channels);
  serializer.Integer("Bits per sample", bits_per_sample);
}

void AudioPlayResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Stream ID", stream_id);
}

void AudioStopRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Stream ID", stream_id);
}

void AudioVolumeRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Float("Volume", volume);
}

void AudioVolumeResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Float("Volume", volume);
}

AudioStreamID PlayAudio(std::shared_ptr<SharedMemory> buffer, float volume,
                        bool loop, uint32 sample_rate, uint8 channels,
                        uint8 bits_per_sample) {
  AudioPlayRequest request;
  request.shared_buffer = buffer;
  request.volume = volume;
  request.loop = loop;
  request.sample_rate = sample_rate;
  request.channels = channels;
  request.bits_per_sample = bits_per_sample;

  auto response_or = GetService<AudioManager>().PlayAudio(request);
  if (!response_or) return 0;
  return response_or->stream_id;
}

void StopAudio(AudioStreamID stream_id) {
  AudioStopRequest request;
  request.stream_id = stream_id;
  (void)GetService<AudioManager>().StopAudio(request, nullptr);
}

void StopAllAudio() { (void)GetService<AudioManager>().StopAllAudio(nullptr); }

void SetAudioVolume(float volume) {
  AudioVolumeRequest request;
  request.volume = volume;
  (void)GetService<AudioManager>().SetVolume(request, nullptr);
}

float GetAudioVolume() {
  auto response_or = GetService<AudioManager>().GetVolume();
  if (!response_or) return 1.0f;
  return response_or->volume;
}

}  // namespace perception
