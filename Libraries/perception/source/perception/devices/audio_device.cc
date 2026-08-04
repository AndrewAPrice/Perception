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

#include "perception/devices/audio_device.h"

#include "perception/serialization/serializer.h"

namespace perception {
namespace devices {

void AudioDeviceDetails::Serialize(serialization::Serializer& serializer) {
  serializer.String("Name", name);
  serializer.Integer("Sample rate", sample_rate);
  serializer.Integer("Channels", channels);
  serializer.Integer("Bits per sample", bits_per_sample);
  serializer.Integer("Buffer size", buffer_size);
}

void AudioDevicePlayRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Serializable("Shared buffer", shared_buffer);
  serializer.Integer("Loop", loop);
  serializer.Float("Volume", volume);
  serializer.Integer("Sample rate", sample_rate);
  serializer.Integer("Channels", channels);
  serializer.Integer("Bits per sample", bits_per_sample);
}

void AudioDevicePlayResponse::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Stream ID", stream_id);
}

void AudioDeviceStopRequest::Serialize(serialization::Serializer& serializer) {
  serializer.Integer("Stream ID", stream_id);
}

void AudioDeviceSetVolumeRequest::Serialize(
    serialization::Serializer& serializer) {
  serializer.Float("Volume", volume);
}

void AudioDeviceGetVolumeResponse::Serialize(
    serialization::Serializer& serializer) {
  serializer.Float("Volume", volume);
}

}  // namespace devices
}  // namespace perception
