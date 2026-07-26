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

#include <map>
#include <mutex>
#include <set>
#include <vector>

#include "perception/audio_manager.h"
#include "perception/devices/audio_device.h"
#include "perception/processes.h"
#include "perception/services.h"

class AudioManager : public ::perception::AudioManager::Server {
 public:
  AudioManager();
  virtual ~AudioManager();

  StatusOr<perception::AudioPlayResponse> PlayAudio(
      const perception::AudioPlayRequest& request,
      perception::ProcessId sender) override;

  Status StopAudio(const perception::AudioStopRequest& request,
                   perception::ProcessId sender) override;

  Status StopAllAudio(perception::ProcessId sender) override;

  Status SetVolume(const perception::AudioVolumeRequest& request,
                   perception::ProcessId sender) override;

  StatusOr<perception::AudioVolumeResponse> GetVolume(
      perception::ProcessId sender) override;

 private:
  void OnHardwareAudioDeviceDiscovered(
      perception::devices::AudioDevice::Client audio_device);
  void OnHardwareAudioDeviceDisappeared(perception::MessageId message_id);
  void OnProcessTerminated(perception::ProcessId pid);

  std::mutex mutex_;
  std::vector<perception::devices::AudioDevice::Client> audio_devices_;
  perception::MessageId notify_device_id_ = 0;

  struct ProcessStream {
    perception::devices::AudioDevice::Client device;
    uint64 hardware_stream_id;
  };

  std::map<uint64, ProcessStream> active_streams_;
  std::map<perception::ProcessId, std::set<uint64>> process_streams_;
  std::map<perception::ProcessId, perception::MessageId> termination_handlers_;
  uint64 next_stream_id_ = 1;
  float master_volume_ = 1.0f;
};
