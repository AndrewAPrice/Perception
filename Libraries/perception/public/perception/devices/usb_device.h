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

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/service_macros.h"
#include "perception/shared_memory.h"
#include "types.h"

namespace perception {
namespace serialization {
class Serializer;
}

namespace devices {

// Total number of TRB entries in an endpoint transfer ring (including Link TRB).
constexpr size_t kUsbTransferRingEntries = 64;

// Index of the Link TRB at the end of the endpoint transfer ring.
constexpr size_t kUsbTransferRingLinkIndex = kUsbTransferRingEntries - 1;

// Byte offset of the shared control header inside the 4KB endpoint ring page.
constexpr size_t kUsbRingHeaderOffset = 1024;

// Byte offset used with SharedMemory::RegisterEvent and TriggerEvent.
constexpr size_t kUsbRingEventOffset = 1024;

// Maximum number of completion entries buffered in the shared ring header.
constexpr size_t kUsbMaxCompletions = 32;

// Byte offset of the DMA data buffer inside the 4KB endpoint ring page.
constexpr size_t kUsbDmaBufferOffset = 2048;

// Size in bytes of the DMA data buffer inside the 4KB endpoint ring page.
constexpr size_t kUsbDmaBufferSize = 2048;

// Standard 16-byte xHCI Transfer Request Block (TRB).
struct UsbTrb {
  uint64 parameter;
  uint32 status;
  uint32 control;
} __attribute__((packed));

// Completion entry written by Device Manager when an endpoint transfer finishes.
struct UsbTransferCompletion {
  uint64 trb_phys;
  uint32 completion_code;
  uint32 residual_length;
};

// Shared control header located at kUsbRingHeaderOffset in the shared ring page.
struct UsbEndpointRingSharedHeader {
  std::atomic<uint32> completion_write_index;
  std::atomic<uint32> completion_read_index;
  std::atomic<uint32> device_disconnected;
  uint32 reserved;
  UsbTransferCompletion completions[kUsbMaxCompletions];
};

// Information describing an enumerated USB interface.
class UsbInterfaceInfo : public serialization::Serializable {
 public:
  uint32 device_handle = 0;
  uint16 vendor_id = 0;
  uint16 product_id = 0;
  uint8 device_speed = 0;
  uint8 interface_number = 0;
  uint8 alternate_setting = 0;
  uint8 interface_class = 0;
  uint8 interface_subclass = 0;
  uint8 interface_protocol = 0;
  uint8 interrupt_in_endpoint_address = 0;
  uint8 interrupt_in_dci = 0;
  uint16 max_packet_size = 0;
  uint8 interval = 0;
  std::string hid_report_descriptor;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// List of enumerated USB interfaces.
class UsbInterfaces : public serialization::Serializable {
 public:
  std::vector<UsbInterfaceInfo> interfaces;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Filter for querying enumerated USB interfaces.
class UsbInterfaceFilter : public serialization::Serializable {
 public:
  int16 interface_class = -1;
  int16 interface_subclass = -1;
  int16 interface_protocol = -1;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Identifier of a USB device for disconnect notifications.
class UsbDeviceId : public serialization::Serializable {
 public:
  uint32 device_handle = 0;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Request to open an xHCI interrupt IN endpoint ring in shared memory.
class OpenUsbEndpointRequest : public serialization::Serializable {
 public:
  uint32 device_handle = 0;
  uint8 interface_number = 0;
  uint8 endpoint_dci = 0;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Shared memory ring and doorbell details for an opened xHCI endpoint.
class UsbEndpointRingInfo : public serialization::Serializable {
 public:
  std::shared_ptr<SharedMemory> ring_memory;
  uint64 ring_phys_address = 0;
  uint64 doorbell_phys_address = 0;
  uint32 doorbell_value = 0;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Request to perform a USB Control Transfer on Endpoint 0.
class UsbControlTransferRequest : public serialization::Serializable {
 public:
  uint32 device_handle = 0;
  uint8 request_type = 0;
  uint8 request = 0;
  uint16 value = 0;
  uint16 index = 0;
  uint16 length = 0;
  std::string data;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

// Response from a USB Control Transfer on Endpoint 0.
class UsbControlTransferResponse : public serialization::Serializable {
 public:
  std::string data;

  virtual void Serialize(serialization::Serializer& serializer) override;
};

#define METHOD_LIST(X)                            \
  X(1, UsbInterfaceAttached, void, UsbInterfaceInfo) \
  X(2, UsbInterfaceDetached, void, UsbDeviceId)

DEFINE_PERCEPTION_SERVICE(UsbDeviceListener,
                          "perception.devices.UsbDeviceListener", METHOD_LIST)
#undef METHOD_LIST

}  // namespace devices
}  // namespace perception
