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

#include <cstddef>
#include <functional>
#include <memory>

#include "perception/devices/usb_device.h"
#include "types.h"

// Manages a shared-memory xHCI Interrupt IN Endpoint Transfer Ring and Doorbell.
class UsbRingEndpoint {
 public:
  UsbRingEndpoint(const ::perception::devices::UsbEndpointRingInfo& ring_info,
                  uint16 max_packet_size,
                  std::function<void(const uint8*, size_t)> on_report,
                  std::function<void()> on_disconnect);
  ~UsbRingEndpoint();

  // Starts queuing Interrupt IN TRBs on the endpoint transfer ring.
  void Enable();

  // Stops queuing new Interrupt IN TRBs after any in-flight transfer finishes.
  void Disable();

  // Returns whether the endpoint is currently enabled.
  bool IsEnabled() const { return is_enabled_; }

 private:
  std::shared_ptr<::perception::SharedMemory> ring_memory_;
  uint64 ring_phys_address_ = 0;
  uint64 dma_phys_address_ = 0;
  uint32 doorbell_value_ = 0;
  uint16 max_packet_size_ = 8;

  ::perception::devices::UsbTrb* trb_ring_ = nullptr;
  ::perception::devices::UsbEndpointRingSharedHeader* shared_header_ = nullptr;
  uint8* dma_buffer_ = nullptr;
  volatile uint32* doorbell_reg_ = nullptr;

  size_t enqueue_index_ = 0;
  uint32 producer_cycle_state_ = 1;
  bool is_enabled_ = false;
  bool transfer_in_flight_ = false;
  bool disconnected_ = false;
  ::perception::MessageId event_message_id_ = 0;

  std::function<void(const uint8*, size_t)> on_report_;
  std::function<void()> on_disconnect_;

  void QueueNextTransfer();
  void ProcessCompletions();
};
