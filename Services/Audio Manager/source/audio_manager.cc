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

#include "audio_manager.h"

#include <iostream>

#include "perception/permissions.h"
#include "status.h"

using ::perception::AudioPlayRequest;
using ::perception::AudioPlayResponse;
using ::perception::AudioStopRequest;
using ::perception::AudioVolumeRequest;
using ::perception::AudioVolumeResponse;
using ::perception::DoesProcessHavePermission;
using ::perception::MessageId;
using ::perception::NotifyOnEachNewServiceInstance;
using ::perception::NotifyUponProcessTermination;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::StopNotifyingOnEachNewServiceInstance;
using ::perception::StopNotifyingUponProcessTermination;
using ::perception::devices::AudioDevice;
using ::perception::devices::AudioDevicePlayRequest;
using ::perception::devices::AudioDeviceSetVolumeRequest;
using ::perception::devices::AudioDeviceStopRequest;

AudioManager::AudioManager() {
  notify_device_id_ = NotifyOnEachNewServiceInstance<AudioDevice>(
      [this](AudioDevice::Client audio_device) {
        OnHardwareAudioDeviceDiscovered(audio_device);
      });
}

AudioManager::~AudioManager() {
  if (notify_device_id_ != 0) {
    StopNotifyingOnEachNewServiceInstance(notify_device_id_);
  }
}

void AudioManager::OnHardwareAudioDeviceDiscovered(
    AudioDevice::Client audio_device) {
  std::scoped_lock lock(mutex_);
  audio_devices_.push_back(audio_device);

  AudioDeviceSetVolumeRequest dev_req;
  dev_req.volume = master_volume_;
  (void)audio_device.SetVolume(dev_req, nullptr);

  std::cout << "Registered new hardware AudioDevice instance." << std::endl;
}

void AudioManager::OnProcessTerminated(ProcessId pid) {
  std::scoped_lock lock(mutex_);
  auto it = process_streams_.find(pid);
  if (it != process_streams_.end()) {
    for (uint64 manager_stream_id : it->second) {
      auto stream_it = active_streams_.find(manager_stream_id);
      if (stream_it != active_streams_.end()) {
        AudioDeviceStopRequest stop_req;
        stop_req.stream_id = stream_it->second.hardware_stream_id;
        (void)stream_it->second.device.StopAudio(stop_req);
        active_streams_.erase(stream_it);
      }
    }
    process_streams_.erase(it);
  }

  auto term_it = termination_handlers_.find(pid);
  if (term_it != termination_handlers_.end()) {
    StopNotifyingUponProcessTermination(term_it->second);
    termination_handlers_.erase(term_it);
  }
}

StatusOr<AudioPlayResponse> AudioManager::PlayAudio(
    const AudioPlayRequest& request, ProcessId sender) {
  if (!DoesProcessHavePermission(sender,
                                 ::perception::Permission::CanPlayAudio)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(mutex_);
  if (audio_devices_.empty()) return Status::SERVICE_DOESNT_EXIST;

  // Forward request to primary hardware device
  AudioDevice::Client primary_device = audio_devices_.front();
  AudioDevicePlayRequest dev_req;
  dev_req.shared_buffer = request.shared_buffer;
  dev_req.loop = request.loop;
  dev_req.volume = request.volume;
  dev_req.sample_rate = request.sample_rate;
  dev_req.channels = request.channels;
  dev_req.bits_per_sample = request.bits_per_sample;

  ASSIGN_OR_RETURN(auto dev_resp, primary_device.PlayAudio(dev_req));

  uint64 manager_stream_id = next_stream_id_++;
  ProcessStream ps;
  ps.device = primary_device;
  ps.hardware_stream_id = dev_resp.stream_id;

  active_streams_[manager_stream_id] = ps;
  process_streams_[sender].insert(manager_stream_id);

  if (termination_handlers_.find(sender) == termination_handlers_.end()) {
    MessageId mid = NotifyUponProcessTermination(
        sender, [this, sender]() { OnProcessTerminated(sender); });
    termination_handlers_[sender] = mid;
  }

  AudioPlayResponse response;
  response.stream_id = manager_stream_id;
  return response;
}

Status AudioManager::StopAudio(const AudioStopRequest& request,
                               ProcessId sender) {
  std::scoped_lock lock(mutex_);
  auto it = active_streams_.find(request.stream_id);
  if (it != active_streams_.end()) {
    AudioDeviceStopRequest stop_req;
    stop_req.stream_id = it->second.hardware_stream_id;
    (void)it->second.device.StopAudio(stop_req, nullptr);

    process_streams_[sender].erase(request.stream_id);
    active_streams_.erase(it);
  }
  return Status::OK;
}

Status AudioManager::StopAllAudio(ProcessId sender) {
  OnProcessTerminated(sender);
  return Status::OK;
}

Status AudioManager::SetVolume(const AudioVolumeRequest& request,
                               ProcessId sender) {
  if (!DoesProcessHavePermission(sender, Permission::CanAdjustVolume)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(mutex_);
  master_volume_ = request.volume;

  AudioDeviceSetVolumeRequest dev_req;
  dev_req.volume = master_volume_;
  for (auto& device : audio_devices_) (void)device.SetVolume(dev_req, nullptr);

  return Status::OK;
}

StatusOr<AudioVolumeResponse> AudioManager::GetVolume(ProcessId sender) {
  std::scoped_lock lock(mutex_);
  AudioVolumeResponse response;
  response.volume = master_volume_;
  return response;
}
