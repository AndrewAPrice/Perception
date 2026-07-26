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

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"
#include "types.h"

namespace perception {
namespace serialization {
class Serializer;
}

class AudioPlayRequest : public serialization::Serializable {
 public:
  std::shared_ptr<SharedMemory> shared_buffer;
  bool loop = false;
  float volume = 1.0f;
  uint32 sample_rate = 44100;
  uint8 channels = 2;
  uint8 bits_per_sample = 16;

  virtual ~AudioPlayRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioPlayResponse : public serialization::Serializable {
 public:
  uint64 stream_id = 0;

  virtual ~AudioPlayResponse() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioStopRequest : public serialization::Serializable {
 public:
  uint64 stream_id = 0;

  virtual ~AudioStopRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioVolumeRequest : public serialization::Serializable {
 public:
  float volume = 1.0f;

  virtual ~AudioVolumeRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioVolumeResponse : public serialization::Serializable {
 public:
  float volume = 1.0f;

  virtual ~AudioVolumeResponse() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                 \
  X(1, PlayAudio, AudioPlayResponse, AudioPlayRequest) \
  X(2, StopAudio, void, AudioStopRequest)              \
  X(3, StopAllAudio, void, void)                       \
  X(4, SetVolume, void, AudioVolumeRequest)            \
  X(5, GetVolume, AudioVolumeResponse, void)

DEFINE_PERCEPTION_SERVICE(AudioManager, "perception.AudioManager", METHOD_LIST)
#undef METHOD_LIST

typedef uint64 AudioStreamID;

AudioStreamID PlayAudio(std::shared_ptr<SharedMemory> buffer,
                        float volume = 1.0f, bool loop = false,
                        uint32 sample_rate = 44100, uint8 channels = 2,
                        uint8 bits_per_sample = 16);

void StopAudio(AudioStreamID stream_id);

void StopAllAudio();

void SetAudioVolume(float volume);

float GetAudioVolume();

}  // namespace perception
