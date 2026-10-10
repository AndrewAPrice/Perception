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

#include "usb_ring_endpoint.h"

#include <algorithm>

#ifndef TEST
#include "perception/cache.h"
#endif
#include "perception/memory.h"
#include "perception/messages.h"

using ::perception::GenerateUniqueMessageId;
using ::perception::kPageSize;
using ::perception::MapPhysicalMemory;
using ::perception::MessageData;
using ::perception::ProcessId;
using ::perception::RegisterMessageHandler;
using ::perception::UnregisterMessageHandler;
using ::perception::devices::kUsbDmaBufferOffset;
using ::perception::devices::kUsbDmaBufferSize;
using ::perception::devices::kUsbMaxCompletions;
using ::perception::devices::kUsbRingEventOffset;
using ::perception::devices::kUsbRingHeaderOffset;
using ::perception::devices::kUsbTransferRingLinkIndex;
using ::perception::devices::UsbEndpointRingInfo;
using ::perception::devices::UsbEndpointRingSharedHeader;
using ::perception::devices::UsbTrb;

namespace {

// Mask for 4KB page offset.
constexpr size_t kPageOffsetMask = kPageSize - 1;

// xHCI TRB Type: Normal (1).
constexpr uint32 kXhciTrbTypeNormal = 1;

// xHCI TRB Type: Link (6).
constexpr uint32 kXhciTrbTypeLink = 6;

// Shift for TRB Type field in TRB Control word.
constexpr uint32 kTrbTypeShift = 10;

// TRB Control bit: Toggle Cycle (for Link TRBs).
constexpr uint32 kTrbLinkToggleCycle = 1U << 1;

// TRB Control bit: Interrupt on Short Packet (ISP).
constexpr uint32 kTrbIspBit = 1U << 2;

// TRB Control bit: Interrupt On Completion (IOC).
constexpr uint32 kTrbIocBit = 1U << 5;

// xHCI Completion Code: Success (1).
constexpr uint32 kXhciCompSuccess = 1;

// xHCI Completion Code: Short Packet (13).
constexpr uint32 kXhciCompShortPacket = 13;

void FlushMemory(void* addr, size_t size) {
#ifndef TEST
  ::perception::FlushRange(addr, size);
#else
  (void)addr;
  (void)size;
#endif
}

}  // namespace

UsbRingEndpoint::UsbRingEndpoint(
    const UsbEndpointRingInfo& ring_info, uint16 max_packet_size,
    std::function<void(const uint8*, size_t)> on_report,
    std::function<void()> on_disconnect)
    : ring_memory_(ring_info.ring_memory),
      ring_phys_address_(ring_info.ring_phys_address),
      dma_phys_address_(ring_info.ring_phys_address + kUsbDmaBufferOffset),
      doorbell_value_(ring_info.doorbell_value),
      max_packet_size_(std::clamp<uint16>(max_packet_size, 1, kUsbDmaBufferSize)),
      on_report_(std::move(on_report)),
      on_disconnect_(std::move(on_disconnect)) {
  if (!ring_memory_ || !ring_memory_->Join()) return;

  uint8* base = static_cast<uint8*>(**ring_memory_);
  trb_ring_ = reinterpret_cast<UsbTrb*>(base);
  shared_header_ =
      reinterpret_cast<UsbEndpointRingSharedHeader*>(base + kUsbRingHeaderOffset);
  dma_buffer_ = base + kUsbDmaBufferOffset;

  if (ring_info.doorbell_phys_address != 0) {
    size_t db_page = ring_info.doorbell_phys_address & ~kPageOffsetMask;
    size_t db_offset = ring_info.doorbell_phys_address & kPageOffsetMask;
    void* mapped = MapPhysicalMemory(db_page, 1);
    if (mapped) {
      doorbell_reg_ = reinterpret_cast<volatile uint32*>(
          static_cast<uint8*>(mapped) + db_offset);
    }
  }

  event_message_id_ = GenerateUniqueMessageId();
  RegisterMessageHandler(
      event_message_id_,
      [this](ProcessId, const MessageData&) { ProcessCompletions(); });
  ring_memory_->RegisterEvent(kUsbRingEventOffset, event_message_id_);
}

UsbRingEndpoint::~UsbRingEndpoint() {
  is_enabled_ = false;
  if (ring_memory_ && event_message_id_ != 0) {
    ring_memory_->UnregisterEvent(kUsbRingEventOffset);
    UnregisterMessageHandler(event_message_id_);
  }
}

void UsbRingEndpoint::Enable() {
  if (disconnected_ || is_enabled_) return;
  is_enabled_ = true;
  if (!transfer_in_flight_) QueueNextTransfer();
}

void UsbRingEndpoint::Disable() { is_enabled_ = false; }

void UsbRingEndpoint::QueueNextTransfer() {
  if (!is_enabled_ || disconnected_ || !trb_ring_ || !doorbell_reg_) return;

  if (ring_memory_ && event_message_id_ != 0)
    ring_memory_->RegisterEvent(kUsbRingEventOffset, event_message_id_);

  transfer_in_flight_ = true;
  UsbTrb& trb = trb_ring_[enqueue_index_];
  trb.parameter = dma_phys_address_;
  trb.status = max_packet_size_;
  trb.control = (kXhciTrbTypeNormal << kTrbTypeShift) | kTrbIocBit |
                kTrbIspBit | producer_cycle_state_;
  FlushMemory(&trb, sizeof(UsbTrb));

  enqueue_index_++;
  if (enqueue_index_ >= kUsbTransferRingLinkIndex) {
    UsbTrb& link = trb_ring_[kUsbTransferRingLinkIndex];
    link.control = (kXhciTrbTypeLink << kTrbTypeShift) | kTrbLinkToggleCycle |
                   producer_cycle_state_;
    FlushMemory(&link, sizeof(UsbTrb));
    enqueue_index_ = 0;
    producer_cycle_state_ ^= 1;
  }

  *doorbell_reg_ = doorbell_value_;
}

void UsbRingEndpoint::ProcessCompletions() {
  if (disconnected_ || !shared_header_) return;

  if (ring_memory_ && event_message_id_ != 0)
    ring_memory_->RegisterEvent(kUsbRingEventOffset, event_message_id_);

  if (shared_header_->device_disconnected.load(std::memory_order_acquire) !=
      0) {
    disconnected_ = true;
    is_enabled_ = false;
    transfer_in_flight_ = false;
    if (ring_memory_ && event_message_id_ != 0)
      ring_memory_->UnregisterEvent(kUsbRingEventOffset);
    if (on_disconnect_) on_disconnect_();
    return;
  }

  uint32 read_idx =
      shared_header_->completion_read_index.load(std::memory_order_relaxed);
  uint32 write_idx =
      shared_header_->completion_write_index.load(std::memory_order_acquire);

  while (read_idx != write_idx) {
    const auto& entry =
        shared_header_->completions[read_idx % kUsbMaxCompletions];
    read_idx++;
    shared_header_->completion_read_index.store(read_idx,
                                                std::memory_order_release);
    transfer_in_flight_ = false;

    if (entry.completion_code == kXhciCompSuccess ||
        entry.completion_code == kXhciCompShortPacket) {
      size_t transferred =
          (entry.residual_length < max_packet_size_)
              ? static_cast<size_t>(max_packet_size_ - entry.residual_length)
              : 0;
      if (transferred > 0 && is_enabled_ && on_report_) {
        FlushMemory(dma_buffer_, transferred);
        on_report_(dma_buffer_, transferred);
      }
    }

    if (disconnected_) return;
  }

  if (is_enabled_ && !transfer_in_flight_) QueueNextTransfer();
}
