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
#include <set>

#include "driver.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/graphics_device.h"
#include "perception/pci.h"
#include "perception/shared_memory.h"
#include "types.h"

class VirtioGraphicsDriver : public perception::devices::GraphicsDevice::Server,
                             public Driver {
 public:
  VirtioGraphicsDriver(const perception::devices::PciDevice& device);
  virtual ~VirtioGraphicsDriver() override;

  virtual perception::Status RunCommands(
      const perception::devices::graphics::Commands& commands,
      perception::ProcessId sender) override;

  virtual StatusOr<perception::devices::graphics::CreateTextureResponse>
  CreateTexture(
      const perception::devices::graphics::CreateTextureRequest& request,
      perception::ProcessId sender) override;

  virtual perception::Status DestroyTexture(
      const perception::devices::graphics::TextureReference& request,
      perception::ProcessId sender) override;

  virtual StatusOr<perception::devices::graphics::TextureInformation>
  GetTextureInformation(
      const perception::devices::graphics::TextureReference& request) override;

  virtual perception::Status SetProcessAllowedToDrawToScreen(
      const perception::devices::graphics::
          ProcessAllowedToDrawToScreenParameters& request) override;

  virtual StatusOr<perception::devices::graphics::Size> GetScreenSize()
      override;

  virtual perception::Status SetGraphicsListener(
      const perception::devices::GraphicsListener::Client& listener)
      override;

 private:
  struct Texture {
    perception::ProcessId owner;
    uint32 width;
    uint32 height;
    std::shared_ptr<perception::SharedMemory> shared_memory;
  };

  struct ProcessInformation {
    perception::MessageId on_process_disappear_listener;
    std::set<uint64> textures;
  };

  struct RenderState {
    Texture* source_texture = nullptr;
    Texture* destination_texture = nullptr;
  };

  void RunCommand(
      const perception::devices::graphics::Command& graphics_command,
      perception::ProcessId sender, RenderState& render_state,
      bool& screen_modified);

  void SetDestinationTexture(perception::ProcessId sender, uint64 texture_id,
                             RenderState& render_state);

  void SetSourceTexture(uint64 texture_id, RenderState& render_state);

  void BitBlt(perception::ProcessId sender, const RenderState& render_state,
              uint32 left_source, uint32 top_source, uint32 left_destination,
              uint32 top_destination, uint32 width_to_copy,
              uint32 height_to_copy, bool alpha_blend);

  void BitBltToBuffer(uint8* source, uint32 source_width, uint32 source_height,
                      uint8* destination, uint32 destination_width,
                      uint32 destination_height, uint32 destination_pitch,
                      uint32 left_source, uint32 top_source,
                      uint32 left_destination, uint32 top_destination,
                      uint32 width_to_copy, uint32 height_to_copy,
                      bool alpha_blend);

  void FillRectangle(uint32 left, uint32 top, uint32 right, uint32 bottom,
                     uint32 color, RenderState& render_state);

  void ReleaseAllResourcesBelongingToProcess(perception::ProcessId process);

  void FlushScreen();

  void CreateScanoutResource();

  void HandleInterrupt();
  void CheckForResolutionChange();

  bool SendCommand(const void* req_data, size_t req_len, void* resp_data,
                   size_t resp_len);

  perception::devices::PciDevice device_;
  uint16 io_base_;

  struct QueueDetails {
    struct VirtQueueDesc {
      uint64 addr;
      uint32 len;
      uint16 flags;
      uint16 next;
    } __attribute__((packed));

    struct VirtQueueAvail {
      uint16 flags;
      uint16 idx;
      uint16 ring[256];
    } __attribute__((packed));

    struct VirtQueueUsedElem {
      uint32 id;
      uint32 len;
    } __attribute__((packed));

    struct VirtQueueUsed {
      uint16 flags;
      uint16 idx;
      VirtQueueUsedElem ring[256];
    } __attribute__((packed));

    uint16 size;
    void* mem;
    size_t phys;
    volatile VirtQueueDesc* desc;
    volatile VirtQueueAvail* avail;
    volatile VirtQueueUsed* used;
    uint16 last_seen_used;
    uint16 next_desc;
    uint16 notify_off;
    void* buffers_virt[256];
    size_t buffers_phys[256];

    void Setup(uint16 queue_idx, uint16 io_base);
    void SetupModern(uint16 queue_idx, volatile uint8* common_cfg);
  };

  std::mutex ctrl_mutex_;
  QueueDetails ctrl_queue_;

  volatile uint8* common_cfg_;
  volatile uint8* notify_cfg_;
  volatile uint8* isr_cfg_;
  uint32 notify_off_multiplier_;

  uint32 screen_width_;
  uint32 screen_height_;
  uint8* framebuffer_;

  std::map<uint64, Texture> textures_;
  std::map<perception::ProcessId, ProcessInformation> process_information_;

  uint64 next_texture_id_;
  perception::ProcessId process_allowed_to_write_to_the_screen_;

  std::unique_ptr<perception::devices::GraphicsListener::Client>
      graphics_listener_;
};
