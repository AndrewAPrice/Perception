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

#include "virtio_input_handler.h"

#include "input_event.h"
#include "perception/cache.h"
#include "perception/memory.h"
#include "queue.h"

using ::perception::FlushRange;
using ::perception::kPageSize;
using ::perception::devices::MouseButton;

namespace {

constexpr uint16 kBtnLeft = 0x110;    // 272
constexpr uint16 kBtnRight = 0x111;   // 273
constexpr uint16 kBtnMiddle = 0x112;  // 274
constexpr uint16 kVringDescFWrite = 2;

constexpr uint32 kVirtioFEventIdx = 1U << 29;
constexpr size_t kCommonCfgMsixConfigOffset = 16;
constexpr size_t kCommonCfgQueueMsixVectorOffset = 26;
constexpr uint16 kVirtioMsixNoVector = 0xFFFF;
constexpr uint16 kEventQueueIndex = 0;
constexpr uint16 kStatusQueueIndex = 1;

}  // namespace

void VirtioInputHandler::Enable(VirtioPciDevice& pci_device) {
  if (is_enabled_) return;

  pci_device.NegotiateFeatures(kVirtioFEventIdx);  // Disable VIRTIO_F_EVENT_IDX

  if (pci_device.is_modern()) {
    volatile uint8* common_cfg = pci_device.common_cfg();
    *(volatile uint16*)(&common_cfg[kCommonCfgMsixConfigOffset]) =
        kVirtioMsixNoVector;
    event_queue_.SetupModern(kEventQueueIndex, common_cfg);
    *(volatile uint16*)(&common_cfg[kCommonCfgQueueMsixVectorOffset]) =
        kVirtioMsixNoVector;

    status_queue_.SetupModern(kStatusQueueIndex, common_cfg);
    *(volatile uint16*)(&common_cfg[kCommonCfgQueueMsixVectorOffset]) =
        kVirtioMsixNoVector;

    for (size_t i = 0; i < event_queue_.size; i++) {
      event_queue_.desc[i].addr = event_queue_.buffers_phys[i];
      event_queue_.desc[i].len = sizeof(InputEvent);
      event_queue_.desc[i].flags = kVringDescFWrite;
      event_queue_.desc[i].next = 0;
      event_queue_.avail->ring[i] = static_cast<uint16>(i);
    }
    event_queue_.avail->idx = event_queue_.size;

    FlushRange((void*)event_queue_.desc, kPageSize);
    FlushRange((void*)event_queue_.avail, kPageSize);
    FlushRange((void*)event_queue_.used, kPageSize);

    pci_device.SetDriverOk();
    pci_device.KickQueue(event_queue_);
  } else if (pci_device.io_base() != 0) {
    pci_device.Reset();
    event_queue_.Setup(kEventQueueIndex, pci_device.io_base());
    status_queue_.Setup(kStatusQueueIndex, pci_device.io_base());

    if (event_queue_.desc && event_queue_.avail) {
      for (size_t i = 0; i < event_queue_.size; i++) {
        event_queue_.desc[i].addr = event_queue_.buffers_phys[i];
        event_queue_.desc[i].len = sizeof(InputEvent);
        event_queue_.desc[i].flags = kVringDescFWrite;
        event_queue_.desc[i].next = 0;
        event_queue_.avail->ring[i] = static_cast<uint16>(i);
      }
      event_queue_.avail->idx = event_queue_.size;
    }
    pci_device.SetDriverOk();
    pci_device.KickQueue(event_queue_);
  }

  is_enabled_ = true;
}

void VirtioInputHandler::Disable(VirtioPciDevice& pci_device) {
  if (!is_enabled_) return;
  pci_device.Reset();
  is_enabled_ = false;
}

MouseButton VirtioInputHandler::MapButton(uint16 code) {
  switch (code) {
    case kBtnLeft:
      return MouseButton::Left;
    case kBtnRight:
      return MouseButton::Right;
    case kBtnMiddle:
      return MouseButton::Middle;
    default:
      return MouseButton::Unknown;
  }
}
