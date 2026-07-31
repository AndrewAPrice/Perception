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
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mixer.h"
#include "perception/devices/audio_device.h"
#include "perception/permissions.h"
#include "perception/shared_memory.h"
#include "types.h"

struct __attribute__((packed)) HdaBdlEntry {
  uint64 address;
  uint32 length;
  uint32 flags;  // IOC = bit 0
};

struct HdaStreamState {
  uint64 stream_id = 0;
  perception::ProcessId owner = 0;
  bool is_active = false;
  bool loop = false;
  bool first_mix = true;
  float volume = 1.0f;
  std::shared_ptr<perception::SharedMemory> shared_buffer;
  size_t play_offset = 0;
  uint32 sample_rate = 48000;
  uint8 channels = 2;
  uint8 bits_per_sample = 16;
};

class IntelHdaController : public ::perception::devices::AudioDevice::Server {
 public:
  IntelHdaController(uint8 bus, uint8 slot, uint8 function);
  virtual ~IntelHdaController();

  bool Initialize();

  StatusOr<::perception::devices::AudioDeviceDetails> GetDeviceDetails(
      perception::ProcessId sender) override;
  StatusOr<::perception::devices::AudioDevicePlayResponse> PlayAudio(
      const ::perception::devices::AudioDevicePlayRequest& request,
      perception::ProcessId sender) override;
  Status StopAudio(const ::perception::devices::AudioDeviceStopRequest& request,
                   perception::ProcessId sender) override;
  Status StopAllAudio(perception::ProcessId sender) override;
  Status SetVolume(
      const ::perception::devices::AudioDeviceSetVolumeRequest& request,
      perception::ProcessId sender) override;
  StatusOr<::perception::devices::AudioDeviceGetVolumeResponse> GetVolume(
      perception::ProcessId sender) override;

 private:
  void OnProcessTerminated(perception::ProcessId pid);

  uint32 SendCodecVerb(uint8 codec, uint8 nid, uint32 verb, uint16 payload);
  void DiscoverCodecNodes(uint8 codec);
  void SetupCodec(uint8 codec);
  void UpdateDmaBuffer();
  void ZeroDmaBufferFrames(size_t start_frame, size_t num_frames);

  uint8 bus_;
  uint8 slot_;
  uint8 function_;
  uint8* mmio_base_ = nullptr;
  uint16 num_iss_ = 0;
  uint16 num_oss_ = 0;
  uint8 active_codec_ = 0;

  // DMA Ring buffer
  HdaBdlEntry* bdl_entries_ = nullptr;
  uint8* dma_buffer_ = nullptr;
  size_t dma_buffer_size_ = 65536;  // 64 KB DMA buffer
  size_t last_frame_ = 0;
  bool first_update_ = true;

  std::mutex stream_mutex_;
  uint64 next_stream_id_ = 1;
  std::vector<HdaStreamState> active_streams_;
  std::map<perception::ProcessId, perception::MessageId> termination_handlers_;
  // Map from ProcessId to a map of shared memory ID -> SharedMemory instances.
  // This caches shared buffers sent by applications so repeated PlayAudio
  // requests do not repeatedly incur page table and TLB mapping/unmapping
  // syscalls.
  std::map<perception::ProcessId,
           std::map<size_t, std::shared_ptr<perception::SharedMemory>>>
      process_buffers_;
  float master_volume_ = 1.0f;

  bool running_ = false;
};
