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

#include "perception/devices/usb_device.h"
#include "types.h"

// xHCI TRB Types.
enum class XhciTrbType : uint32 {
  kNormal = 1,
  kSetupStage = 2,
  kDataStage = 3,
  kStatusStage = 4,
  kLink = 6,
  kEnableSlotCommand = 9,
  kDisableSlotCommand = 10,
  kAddressDeviceCommand = 11,
  kConfigureEndpointCommand = 12,
  kEvaluateContextCommand = 13,
  kResetEndpointCommand = 14,
  kStopEndpointCommand = 15,
  kSetTrDequeuePointerCommand = 16,
  kNoOpCommand = 23,
  kTransferEvent = 32,
  kCommandCompletionEvent = 33,
  kPortStatusChangeEvent = 34,
};

// xHCI Completion Codes.
enum class XhciCompletionCode : uint32 {
  kInvalid = 0,
  kSuccess = 1,
  kDataBufferError = 2,
  kBabbleDetectedError = 3,
  kUsbTransactionError = 4,
  kTrbError = 5,
  kStallError = 6,
  kShortPacket = 13,
};

// xHCI Event Ring Segment Table Entry (16 bytes).
struct XhciErstEntry {
  uint64 ring_segment_base_address;
  uint16 ring_segment_size;
  uint16 reserved0;
  uint32 reserved1;
} __attribute__((packed));

// xHCI 32-byte Input Control Context structure.
struct XhciInputControlContext {
  uint32 drop_context_flags;
  uint32 add_context_flags;
  uint32 reserved[5];
  uint32 config_value_interface_alt;
} __attribute__((packed));

// xHCI 32-byte Slot Context structure.
struct XhciSlotContext {
  uint32 route_speed_entries;
  uint32 latency_root_port_num_ports;
  uint32 tt_info_interrupter;
  uint32 device_address_slot_state;
  uint32 reserved[4];
} __attribute__((packed));

// xHCI 32-byte Endpoint Context structure.
struct XhciEndpointContext {
  uint32 ep_state_mult_interval;
  uint32 ep_type_cerr_max_packet_size;
  uint64 tr_dequeue_pointer;
  uint32 average_trb_length_max_esit;
  uint32 reserved[3];
} __attribute__((packed));

// Standard 18-byte USB Device Descriptor.
struct UsbDeviceDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint16 usb_bcd;
  uint8 device_class;
  uint8 device_subclass;
  uint8 device_protocol;
  uint8 max_packet_size_0;
  uint16 vendor_id;
  uint16 product_id;
  uint16 device_bcd;
  uint8 manufacturer_index;
  uint8 product_index;
  uint8 serial_number_index;
  uint8 num_configurations;
} __attribute__((packed));

// Standard 9-byte USB Configuration Descriptor header.
struct UsbConfigDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint16 total_length;
  uint8 num_interfaces;
  uint8 configuration_value;
  uint8 configuration_index;
  uint8 attributes;
  uint8 max_power;
} __attribute__((packed));

// Standard 9-byte USB Interface Descriptor.
struct UsbInterfaceDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint8 interface_number;
  uint8 alternate_setting;
  uint8 num_endpoints;
  uint8 interface_class;
  uint8 interface_subclass;
  uint8 interface_protocol;
  uint8 interface_index;
} __attribute__((packed));

// Standard 7-byte USB Endpoint Descriptor.
struct UsbEndpointDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint8 endpoint_address;
  uint8 attributes;
  uint16 max_packet_size;
  uint8 interval;
} __attribute__((packed));

// Standard 9-byte USB HID Descriptor header.
struct UsbHidDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint16 hid_bcd;
  uint8 country_code;
  uint8 num_descriptors;
  uint8 class_descriptor_type;
  uint16 class_descriptor_length;
} __attribute__((packed));

// Standard USB Hub Descriptor header.
struct UsbHubDescriptor {
  uint8 length;
  uint8 descriptor_type;
  uint8 num_ports;
  uint16 hub_characteristics;
  uint8 power_on_to_power_good;
  uint8 hub_controller_current;
} __attribute__((packed));
