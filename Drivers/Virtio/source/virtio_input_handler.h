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

#include "input_event.h"
#include "perception/cache.h"
#include "perception/devices/mouse_device.h"
#include "perception/memory.h"
#include "queue.h"
#include "virtio_pci_device.h"

class VirtioInputHandler {
 public:
  VirtioInputHandler() = default;

  void Enable(VirtioPciDevice& pci_device);
  void Disable(VirtioPciDevice& pci_device);

  bool is_enabled() const { return is_enabled_; }

  QueueDetails& event_queue() { return event_queue_; }
  QueueDetails& status_queue() { return status_queue_; }

  template <typename ProcessFn>
  void HandleInterrupt(VirtioPciDevice& pci_device, ProcessFn&& process_fn) {
    if (!is_enabled_) return;

    if (pci_device.isr_cfg() != nullptr) {
      (void)*pci_device.isr_cfg();
    }

    uint16 newly_added_buffers = 0;

    while (event_queue_.last_seen_used != event_queue_.used->idx) {
      uint16 used_idx = event_queue_.last_seen_used % event_queue_.size;
      uint32 desc_id = event_queue_.used->ring[used_idx].id;
      event_queue_.last_seen_used++;

      if (desc_id < event_queue_.size) {
        InputEvent ev = *(InputEvent*)event_queue_.buffers_virt[desc_id];
        process_fn(ev);

        uint16 avail_slot = event_queue_.avail->idx % event_queue_.size;
        event_queue_.avail->ring[avail_slot] = static_cast<uint16>(desc_id);
        event_queue_.avail->idx = event_queue_.avail->idx + 1;
        newly_added_buffers++;
      }
    }

    if (newly_added_buffers > 0) {
      perception::FlushRange((void*)event_queue_.avail, perception::kPageSize);
      pci_device.KickQueue(event_queue_);
    }
  }

  static perception::devices::MouseButton MapButton(uint16 code);

 private:
  QueueDetails event_queue_;
  QueueDetails status_queue_;
  bool is_enabled_ = false;
};
