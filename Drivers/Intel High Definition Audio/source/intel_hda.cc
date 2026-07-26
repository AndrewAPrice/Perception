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

#include "intel_hda.h"

#include <algorithm>
#include <cstring>
#include <iostream>

#include "perception/cache.h"
#include "perception/fibers.h"
#include "perception/memory.h"
#include "perception/pci.h"
#include "perception/time.h"

using ::Status;
using ::StatusOr;
using ::perception::DoesProcessHavePermission;
using ::perception::Fiber;
using ::perception::kPciHdrBar0;
using ::perception::kPciHdrCommand;
using ::perception::kPciHdrCommandBitBusMaster;
using ::perception::kPciHdrCommandBitMemorySpace;
using ::perception::MapPhysicalMemory;
using ::perception::Permission;
using ::perception::ProcessId;
using ::perception::Read16BitsFromPciConfig;
using ::perception::Read32BitsFromPciConfig;
using ::perception::SleepForDuration;
using ::perception::Write16BitsToPciConfig;

namespace devices = ::perception::devices;

namespace {

// Register Offsets
constexpr uint32 kRegGCAP = 0x00;
constexpr uint32 kRegVMIN = 0x02;
constexpr uint32 kRegVMAJ = 0x03;
constexpr uint32 kRegGCTL = 0x08;
constexpr uint32 kRegWAKEEN = 0x0C;
constexpr uint32 kRegSTATESTS = 0x0E;
constexpr uint32 kRegGSTS = 0x10;
constexpr uint32 kRegICOI = 0x60;
constexpr uint32 kRegICII = 0x64;
constexpr uint32 kRegICIS = 0x68;

}  // namespace

IntelHdaController::IntelHdaController(uint8 bus, uint8 slot, uint8 function)
    : bus_(bus), slot_(slot), function_(function) {}

IntelHdaController::~IntelHdaController() {
  running_ = false;
  if (dma_buffer_) {
    perception::ReleaseMemoryPages(dma_buffer_,
                                   dma_buffer_size_ / perception::kPageSize);
    dma_buffer_ = nullptr;
  }
  if (bdl_entries_) {
    perception::ReleaseMemoryPages(bdl_entries_, 1);
    bdl_entries_ = nullptr;
  }
}

uint32 IntelHdaController::SendCodecVerb(uint8 codec, uint8 nid, uint32 verb,
                                         uint16 payload) {
  if (!mmio_base_) return 0;

  volatile uint16* icis =
      reinterpret_cast<volatile uint16*>(mmio_base_ + kRegICIS);
  volatile uint32* icoi =
      reinterpret_cast<volatile uint32*>(mmio_base_ + kRegICOI);
  volatile uint32* icii =
      reinterpret_cast<volatile uint32*>(mmio_base_ + kRegICII);

  // Wait for ICIS busy bit (bit 0) to clear
  for (int i = 0; i < 1000; ++i) {
    if ((*icis & 1) == 0) break;
    SleepForDuration(std::chrono::microseconds(10));
  }

  uint32 cmd = 0;
  if (verb <= 0x0F) {
    cmd = ((uint32)(codec & 0xF) << 28) | ((uint32)(nid & 0xFF) << 20) |
          ((uint32)(verb & 0xF) << 16) | (payload & 0xFFFF);
  } else {
    cmd = ((uint32)(codec & 0xF) << 28) | ((uint32)(nid & 0xFF) << 20) |
          ((uint32)(verb & 0xFFF) << 8) | (payload & 0xFF);
  }

  *icis = 2;    // Clear IRV bit
  *icoi = cmd;  // Write command
  *icis = 1;    // Set ICB (start execution)

  // Poll until busy bit (bit 0) clears
  for (int i = 0; i < 1000; ++i) {
    if ((*icis & 1) == 0) break;
    SleepForDuration(std::chrono::microseconds(10));
  }

  return *icii;
}

void IntelHdaController::DiscoverCodecNodes(uint8 codec) {
  uint32 root_sub = SendCodecVerb(codec, 0x00, 0xF00, 0x04);
  uint8 afg_start = (root_sub >> 16) & 0xFF;
  uint8 afg_count = root_sub & 0xFF;

  for (uint8 afg = afg_start; afg < afg_start + afg_count; ++afg) {
    uint32 afg_sub = SendCodecVerb(codec, afg, 0xF00, 0x04);
    uint8 widget_start = (afg_sub >> 16) & 0xFF;
    uint8 widget_count = afg_sub & 0xFF;

    for (uint8 w = widget_start; w < widget_start + widget_count; ++w) {
      SendCodecVerb(codec, w, 0xF00, 0x09);
    }
  }
}

void IntelHdaController::SetupCodec(uint8 codec) {
  // Power state D0 for Root (NID 0) and Audio Function Group (NID 1)
  SendCodecVerb(codec, 0x00, 0x705, 0x00);
  SendCodecVerb(codec, 0x01, 0x705, 0x00);

  // Enable GPIO 0 as Output HIGH to power external amplifier
  SendCodecVerb(codec, 0x01, 0x717, 0x01);  // Set Direction = Output
  SendCodecVerb(codec, 0x01, 0x716, 0x01);  // Set Enable = 1
  SendCodecVerb(codec, 0x01, 0x715, 0x01);  // Set Data = HIGH

  SendCodecVerb(codec, 0x01, 0xF05, 0x00);

  // Configure DAC (NID 2)
  SendCodecVerb(codec, 0x02, 0x705, 0x00);  // Power D0
  SendCodecVerb(codec, 0x02, 0x706, 0x10);  // Bind Stream 1, Channel 0 FIRST
  SendCodecVerb(codec, 0x02, 0x2,
                0x0011);  // Set Format 48kHz 16-bit Stereo (0x0011)
  SendCodecVerb(codec, 0x02, 0x706, 0x10);  // Bind Stream 1, Channel 0 SECOND
  SendCodecVerb(codec, 0x02, 0x3, 0xB07F);  // Unmute Output Amp, max gain 127
  SendCodecVerb(codec, 0x02, 0x3,
                0x307F);  // Unmute Input Amp index 0, max gain 127

  SendCodecVerb(codec, 0x02, 0xF05, 0x00);
  SendCodecVerb(codec, 0x02, 0xB00, 0xA000);  // Get left output amp

  // Configure Pin Complex (NID 3)
  SendCodecVerb(codec, 0x03, 0x705, 0x00);  // Power D0
  SendCodecVerb(codec, 0x03, 0x701, 0x00);  // Select Connection 0 (DAC NID 2)
  SendCodecVerb(codec, 0x03, 0x706, 0x10);  // Bind Stream 1, Channel 0
  SendCodecVerb(codec, 0x03, 0x707, 0xC0);  // Output + Headphone Enable
  SendCodecVerb(codec, 0x03, 0x3, 0xB07F);  // Unmute Output Amp, max gain 127
  SendCodecVerb(codec, 0x03, 0x3,
                0x307F);  // Unmute Input Amp index 0, max gain 127
  SendCodecVerb(codec, 0x03, 0x3,
                0x317F);  // Unmute Input Amp index 1, max gain 127
  SendCodecVerb(codec, 0x03, 0x70C, 0x02);  // EAPD enable
  SendCodecVerb(codec, 0x03, 0x70C, 0x06);  // EAPD + BTL enable

  // Config Default: Connected Line Out / Headphone Jack
  SendCodecVerb(codec, 0x03, 0x71C, 0x10);
  SendCodecVerb(codec, 0x03, 0x71D, 0x10);
  SendCodecVerb(codec, 0x03, 0x71E, 0x01);
  SendCodecVerb(codec, 0x03, 0x71F, 0x01);

  SendCodecVerb(codec, 0x03, 0xF05, 0x00);
  SendCodecVerb(codec, 0x03, 0xB00, 0xA000);  // Get left output amp
  SendCodecVerb(codec, 0x03, 0xF07, 0x00);
}

bool IntelHdaController::Initialize() {
  // Enable Bus Master and Memory Space in 16-bit PCI command register
  uint16 command =
      Read16BitsFromPciConfig(bus_, slot_, function_, kPciHdrCommand);
  command |= (kPciHdrCommandBitBusMaster | kPciHdrCommandBitMemorySpace);
  Write16BitsToPciConfig(bus_, slot_, function_, kPciHdrCommand, command);

  // Read BAR0 MMIO Address
  uint32 bar0 = Read32BitsFromPciConfig(bus_, slot_, function_, kPciHdrBar0);
  size_t phys_base = bar0 & 0xFFFFFFF0;
  if (phys_base == 0) {
    std::cout << "BAR0 physical address is 0! Cannot initialize." << std::endl;
    return false;
  }

  // Map MMIO pages (4 pages = 0x4000 bytes)
  void* mapped = MapPhysicalMemory(phys_base, 4);
  if (!mapped) {
    std::cout << "MapPhysicalMemory failed!" << std::endl;
    return false;
  }
  mmio_base_ = reinterpret_cast<uint8*>(mapped);

  // Reset HDA Controller via GCTL.CRST (bit 0)
  volatile uint32* gctl =
      reinterpret_cast<volatile uint32*>(mmio_base_ + kRegGCTL);
  *gctl = *gctl & ~1U;
  for (int i = 0; i < 100; ++i) {
    if ((*gctl & 1U) == 0) break;
    SleepForDuration(std::chrono::milliseconds(1));
  }
  if ((*gctl & 1U) != 0) {
    std::cout << "Failed to enter reset!" << std::endl;
    return false;
  }

  *gctl = *gctl | 1U;
  for (int i = 0; i < 100; ++i) {
    if ((*gctl & 1U) != 0) break;
    SleepForDuration(std::chrono::milliseconds(1));
  }
  if ((*gctl & 1U) == 0) {
    std::cout << "Failed to exit reset!" << std::endl;
    return false;
  }

  // Read capabilities
  volatile uint16* gcap =
      reinterpret_cast<volatile uint16*>(mmio_base_ + kRegGCAP);
  num_iss_ = (*gcap >> 8) & 0x0F;
  num_oss_ = (*gcap >> 12) & 0x0F;

  // Enable Global Interrupts (GIE bit 31), Controller Interrupts (CIE bit 30),
  // and Stream Interrupts
  volatile uint32* intctl =
      reinterpret_cast<volatile uint32*>(mmio_base_ + 0x20);
  *intctl = 0xC00000FF;

  // Read STATESTS (State Change Status) to find active codecs
  volatile uint16* statests =
      reinterpret_cast<volatile uint16*>(mmio_base_ + kRegSTATESTS);
  for (int i = 0; i < 50; ++i) {
    if (*statests != 0) break;
    SleepForDuration(std::chrono::milliseconds(1));
  }

  uint16 codecs = *statests;
  for (uint8 c = 0; c < 15; ++c) {
    if ((codecs & (1 << c)) != 0) {
      active_codec_ = c;
      DiscoverCodecNodes(c);
      SetupCodec(c);
    }
  }

  // Configure Output Stream 0 (OSD0)
  size_t osd0_offset = 0x80 + (num_iss_ * 0x20);

  volatile uint32* sd0_ctl =
      reinterpret_cast<volatile uint32*>(mmio_base_ + osd0_offset + 0x00);
  volatile uint8* sd0_sts =
      reinterpret_cast<volatile uint8*>(mmio_base_ + osd0_offset + 0x03);
  volatile uint32* sd0_cbl =
      reinterpret_cast<volatile uint32*>(mmio_base_ + osd0_offset + 0x08);
  volatile uint16* sd0_lvi =
      reinterpret_cast<volatile uint16*>(mmio_base_ + osd0_offset + 0x0C);
  volatile uint16* sd0_fmt =
      reinterpret_cast<volatile uint16*>(mmio_base_ + osd0_offset + 0x12);
  volatile uint32* sd0_bdpl =
      reinterpret_cast<volatile uint32*>(mmio_base_ + osd0_offset + 0x18);
  volatile uint32* sd0_bdpu =
      reinterpret_cast<volatile uint32*>(mmio_base_ + osd0_offset + 0x1C);

  // Stream Reset
  *sd0_ctl = *sd0_ctl | 1U;
  SleepForDuration(std::chrono::milliseconds(2));
  *sd0_ctl = *sd0_ctl & ~1U;
  SleepForDuration(std::chrono::milliseconds(2));

  // Clear status register bits (BCIS, FIFOE, DESE)
  *sd0_sts = 0x1C;

  // Set Format = 48kHz, 16-bit, 2 channels (0x0011)
  *sd0_fmt = 0x0011;

  // Allocate DMA ring buffer in physical memory pages BELOW 4GB (<4GB for
  // 32-bit DMA)
  dma_buffer_size_ = 65536;
  size_t dma_pages = dma_buffer_size_ / perception::kPageSize;
  size_t dma_buf_phys = 0;
  dma_buffer_ = reinterpret_cast<uint8*>(
      perception::AllocateMemoryPagesBelowPhysicalAddressBase(
          dma_pages, 0xFFFFFFFF, dma_buf_phys));
  if (!dma_buffer_ || dma_buf_phys == 0) {
    std::cout << "Failed to allocate DMA buffer!" << std::endl;
    return false;
  }
  std::memset(dma_buffer_, 0, dma_buffer_size_);

  // Allocate BDL in physical memory page BELOW 4GB
  size_t bdl_phys = 0;
  bdl_entries_ = reinterpret_cast<HdaBdlEntry*>(
      perception::AllocateMemoryPagesBelowPhysicalAddressBase(1, 0xFFFFFFFF,
                                                              bdl_phys));
  if (!bdl_entries_ || bdl_phys == 0) {
    std::cout << "Failed to allocate BDL entries!" << std::endl;
    return false;
  }
  std::memset(bdl_entries_, 0, perception::kPageSize);

  // Populate BDL entries: 1 entry per 4KB page (16 entries total for 64KB)
  size_t num_bdl_entries = dma_buffer_size_ / perception::kPageSize;  // 16
  for (size_t i = 0; i < num_bdl_entries; ++i) {
    size_t page_virt =
        reinterpret_cast<size_t>(dma_buffer_) + i * perception::kPageSize;
    size_t page_phys =
        perception::GetPhysicalAddressOfVirtualAddress(page_virt);

    bdl_entries_[i].address = static_cast<uint64>(page_phys);
    bdl_entries_[i].length = static_cast<uint32>(perception::kPageSize);
    bdl_entries_[i].flags = 0;  // No IOC interrupts for continuous loop
  }

  *sd0_bdpl = static_cast<uint32>(bdl_phys);
  *sd0_bdpu = 0;
  *sd0_cbl = static_cast<uint32>(dma_buffer_size_);
  *sd0_lvi =
      static_cast<uint16>(num_bdl_entries - 1);  // LVI = 15 for 16 entries

  perception::FlushRange(bdl_entries_, perception::kPageSize);

  running_ = true;
  last_frame_ = 0;
  first_update_ = true;

  // Prefill DMA buffer BEFORE starting RUN bit
  UpdateDmaBuffer();

  // Set Stream ID = 1 (bits 23:20 for QEMU and 19:16 for real HW) first
  *sd0_ctl = (1U << 20) | (1U << 16);
  SleepForDuration(std::chrono::milliseconds(1));

  // Set Stream ID = 1, RUN bit (bit 1 only)
  *sd0_ctl = (1U << 20) | (1U << 16) | 2U;

  // Re-apply codec configuration while stream is active
  SetupCodec(active_codec_);

  // Fiber background loop to continuously mix active streams into DMA buffer
  Fiber::Create([this]() {
    while (running_) {
      UpdateDmaBuffer();
      SleepForDuration(std::chrono::milliseconds(10));
    }
  })->WakeUp();

  return true;
}

void IntelHdaController::UpdateDmaBuffer() {
  std::scoped_lock lock(stream_mutex_);
  if (!mmio_base_ || !dma_buffer_) return;

  size_t osd0_offset = 0x80 + (num_iss_ * 0x20);
  volatile uint8* sd0_sts =
      reinterpret_cast<volatile uint8*>(mmio_base_ + osd0_offset + 0x03);
  volatile uint32* sd0_lpib =
      reinterpret_cast<volatile uint32*>(mmio_base_ + osd0_offset + 0x04);

  uint8 sts = *sd0_sts;
  // Clear completion and error status bits on stream 0
  *sd0_sts = 0x1C;

  constexpr size_t total_frames = 16384;  // 64KB / 4 bytes per frame
  uint32 current_bytes = *sd0_lpib % dma_buffer_size_;
  size_t current_frame = (current_bytes / 4) % total_frames;

  if (first_update_) {
    first_update_ = false;
    last_frame_ = current_frame;
  }

  size_t frames_advanced = 0;
  if (current_frame >= last_frame_) {
    frames_advanced = current_frame - last_frame_;
  } else {
    frames_advanced = (total_frames - last_frame_) + current_frame;
  }

  // Advance stream play offsets by the number of frames hardware consumed
  for (auto it = active_streams_.begin(); it != active_streams_.end();) {
    if (!it->is_active || !it->shared_buffer || !**it->shared_buffer) {
      it = active_streams_.erase(it);
      continue;
    }

    size_t total_src_frames =
        (it->shared_buffer->GetSize() / sizeof(int16_t)) / 2;
    if (total_src_frames == 0) {
      it = active_streams_.erase(it);
      continue;
    }

    it->play_offset += frames_advanced;
    if (it->play_offset >= total_src_frames) {
      if (it->loop) {
        it->play_offset %= total_src_frames;
      } else {
        it->is_active = false;
        it = active_streams_.erase(it);
        continue;
      }
    }
    ++it;
  }

  last_frame_ = current_frame;

  // Render and mix audio into full 64KB DMA ring buffer ahead of current
  // hardware read pointer
  constexpr size_t lookahead_frames = 16384;  // Full 64KB ring buffer (~341ms)
  int16_t* mix_buf = reinterpret_cast<int16_t*>(dma_buffer_);

  for (size_t f = 0; f < lookahead_frames; ++f) {
    size_t target_frame = (current_frame + f) % total_frames;
    mix_buf[target_frame * 2 + 0] = 0;
    mix_buf[target_frame * 2 + 1] = 0;
  }

  for (auto& stream : active_streams_) {
    if (!stream.is_active || !stream.shared_buffer || !**stream.shared_buffer) {
      continue;
    }

    const int16_t* src_pcm =
        reinterpret_cast<const int16_t*>(**stream.shared_buffer);
    size_t total_src_frames =
        (stream.shared_buffer->GetSize() / sizeof(int16_t)) / 2;
    if (!src_pcm || total_src_frames == 0) continue;

    for (size_t f = 0; f < lookahead_frames; ++f) {
      size_t src_frame = (stream.play_offset + f);
      if (src_frame >= total_src_frames) {
        if (stream.loop) {
          src_frame %= total_src_frames;
        } else {
          break;
        }
      }

      int32_t left_sample = static_cast<int32_t>(
          src_pcm[src_frame * 2 + 0] * stream.volume * master_volume_);
      int32_t right_sample = static_cast<int32_t>(
          src_pcm[src_frame * 2 + 1] * stream.volume * master_volume_);

      size_t target_frame = (current_frame + f) % total_frames;

      int32_t cur_left = mix_buf[target_frame * 2 + 0];
      int32_t cur_right = mix_buf[target_frame * 2 + 1];

      int32_t mix_left = std::clamp(cur_left + left_sample, -32768, 32767);
      int32_t mix_right = std::clamp(cur_right + right_sample, -32768, 32767);

      mix_buf[target_frame * 2 + 0] = static_cast<int16_t>(mix_left);
      mix_buf[target_frame * 2 + 1] = static_cast<int16_t>(mix_right);
    }
  }

  perception::FlushRange(dma_buffer_, dma_buffer_size_);
}

StatusOr<devices::AudioDeviceDetails> IntelHdaController::GetDeviceDetails(
    ProcessId sender) {
  if (!DoesProcessHavePermission(sender,
                                 Permission::CanDirectlyControlAudioDevice)) {
    return Status::NOT_ALLOWED;
  }
  devices::AudioDeviceDetails details;
  details.name = "Intel High Definition Audio Controller";
  details.sample_rate = 48000;
  details.channels = 2;
  details.bits_per_sample = 16;
  details.buffer_size = static_cast<uint32>(dma_buffer_size_);
  return details;
}

StatusOr<devices::AudioDevicePlayResponse> IntelHdaController::PlayAudio(
    const devices::AudioDevicePlayRequest& request, ProcessId sender) {
  if (!::DoesProcessHavePermission(sender,
                                   Permission::CanDirectlyControlAudioDevice)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(stream_mutex_);
  HdaStreamState state;
  state.stream_id = next_stream_id_++;
  state.is_active = true;
  state.loop = request.loop;
  state.volume = request.volume;
  state.shared_buffer = request.shared_buffer;
  state.sample_rate = request.sample_rate;
  state.channels = request.channels;
  state.bits_per_sample = request.bits_per_sample;
  state.play_offset = 0;

  active_streams_.push_back(state);

  devices::AudioDevicePlayResponse response;
  response.stream_id = state.stream_id;
  return response;
}

Status IntelHdaController::StopAudio(
    const devices::AudioDeviceStopRequest& request, ProcessId sender) {
  if (!DoesProcessHavePermission(sender,
                                 Permission::CanDirectlyControlAudioDevice)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(stream_mutex_);
  for (auto& stream : active_streams_) {
    if (stream.stream_id == request.stream_id) {
      stream.is_active = false;
      break;
    }
  }
  return Status::OK;
}

Status IntelHdaController::StopAllAudio(ProcessId sender) {
  if (!DoesProcessHavePermission(sender,
                                 Permission::CanDirectlyControlAudioDevice)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(stream_mutex_);
  for (auto& stream : active_streams_) {
    stream.is_active = false;
  }
  active_streams_.clear();
  return Status::OK;
}

Status IntelHdaController::SetVolume(
    const devices::AudioDeviceSetVolumeRequest& request, ProcessId sender) {
  if (!DoesProcessHavePermission(sender,
                                 Permission::CanDirectlyControlAudioDevice)) {
    return Status::NOT_ALLOWED;
  }
  std::scoped_lock lock(stream_mutex_);
  master_volume_ = request.volume;
  return Status::OK;
}
