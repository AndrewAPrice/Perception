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
#include <string>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"
#include "types.h"

namespace perception {
namespace devices {

class AudioDeviceDetails : public serialization::Serializable {
 public:
  std::string name;
  uint32 sample_rate = 48000;
  uint8 channels = 2;
  uint8 bits_per_sample = 16;
  uint32 buffer_size = 4096;

  virtual ~AudioDeviceDetails() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioDevicePlayRequest : public serialization::Serializable {
 public:
  std::shared_ptr<SharedMemory> shared_buffer;
  bool loop = false;
  float volume = 1.0f;
  uint32 sample_rate = 48000;
  uint8 channels = 2;
  uint8 bits_per_sample = 16;

  virtual ~AudioDevicePlayRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioDevicePlayResponse : public serialization::Serializable {
 public:
  uint64 stream_id = 0;

  virtual ~AudioDevicePlayResponse() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioDeviceStopRequest : public serialization::Serializable {
 public:
  uint64 stream_id = 0;

  virtual ~AudioDeviceStopRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioDeviceSetVolumeRequest : public serialization::Serializable {
 public:
  float volume = 1.0f;

  virtual ~AudioDeviceSetVolumeRequest() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

class AudioDeviceGetVolumeResponse : public serialization::Serializable {
 public:
  float volume = 1.0f;

  virtual ~AudioDeviceGetVolumeResponse() = default;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                                             \
  X(1, GetDeviceDetails, AudioDeviceDetails, void)                 \
  X(2, PlayAudio, AudioDevicePlayResponse, AudioDevicePlayRequest) \
  X(3, StopAudio, void, AudioDeviceStopRequest)                    \
  X(4, StopAllAudio, void, void)                                   \
  X(5, SetVolume, void, AudioDeviceSetVolumeRequest)               \
  X(6, GetVolume, AudioDeviceGetVolumeResponse, void)

DEFINE_PERCEPTION_SERVICE(AudioDevice, "perception.devices.AudioDevice",
                          METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception
