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
#include <vector>

#include "types.h"

// Type of USB HID input device represented by an interface or report layout.
enum class HidDeviceType {
  kUnknown,
  kKeyboard,
  kMouse,
  kTablet,
};

// Bit-level descriptor for a single axis or scalar value in an input report.
struct HidAxisField {
  bool present = false;
  uint32 bit_offset = 0;
  uint8 bit_size = 0;
  bool is_signed = false;
  bool is_relative = true;
  int32 logical_min = 0;
  int32 logical_max = 0;
};

// Bit-level descriptor for a 1-bit key or button field in an input report.
struct HidBitUsageField {
  uint32 bit_offset = 0;
  uint16 usage = 0;
};

// Bit-level descriptor for an array of key codes in an input report.
struct HidKeyArrayField {
  uint32 bit_offset = 0;
  uint8 bit_size = 8;
  uint8 count = 0;
  uint16 usage_min = 0;
};

// Parsed layout of a USB HID input report.
struct HidReportLayout {
  HidDeviceType device_type = HidDeviceType::kUnknown;
  bool is_boot_protocol_fallback = false;
  bool uses_report_id = false;
  uint8 report_id = 0;
  size_t report_byte_length = 0;

  // Keyboard fields (Usage Page 0x07).
  std::vector<HidBitUsageField> key_bit_fields;
  std::vector<HidKeyArrayField> key_array_fields;

  // Pointer button fields (index 0 = Left, 1 = Right, 2 = Middle).
  bool button_present[3] = {false, false, false};
  uint32 button_bit_offset[3] = {0, 0, 0};

  // Pointer axis fields.
  HidAxisField x_field;
  HidAxisField y_field;
  HidAxisField wheel_field;
  HidAxisField hwheel_field;
};

// Decoded keyboard state from a single HID input report.
struct ParsedKeyboardReport {
  bool valid = false;
  std::vector<uint8> pressed_keycodes;
};

// Decoded mouse or tablet state from a single HID input report.
struct ParsedPointerReport {
  bool valid = false;
  bool buttons[3] = {false, false, false};
  int32 x = 0;
  int32 y = 0;
  int32 wheel = 0;
  int32 hwheel = 0;
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
};

// Parses a USB HID Report Descriptor (or falls back to Boot Protocol layout).
HidReportLayout ParseHidReportDescriptor(const uint8* data, size_t length,
                                         uint8 interface_subclass,
                                         uint8 interface_protocol);

// Extracts a signed or unsigned integer field of up to 32 bits from a report.
int32 ExtractHidFieldValue(const uint8* report, size_t report_len,
                           uint32 bit_offset, uint8 bit_size, bool is_signed);

// Decodes a keyboard input report into a list of pressed Perception KeyCodes.
ParsedKeyboardReport ParseKeyboardReport(const HidReportLayout& layout,
                                         const uint8* report,
                                         size_t report_len);

// Decodes a mouse or tablet input report into button states and axis values.
ParsedPointerReport ParsePointerReport(const HidReportLayout& layout,
                                       const uint8* report, size_t report_len);

// Maps a USB HID Keyboard Usage Page (0x07) usage ID to a Perception KeyCode.
uint8 HidUsageToKeyCode(uint8 hid_usage);
