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

#include "hid_parser.h"

#include "perception/ui/keyboard.h"
#include "testing.h"

using ::perception::ui::KeyCode;

namespace {

// Standard USB HID Boot/Report Keyboard Descriptor (63 bytes).
constexpr uint8 kStandardKeyboardReportDesc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Minimum (224 - Left Control)
    0x29, 0xE7,  //   Usage Maximum (231 - Right GUI)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x01,  //   Logical Maximum (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Constant)
    0x95, 0x05,  //   Report Count (5)
    0x75, 0x01,  //   Report Size (1)
    0x05, 0x08,  //   Usage Page (LEDs)
    0x19, 0x01,  //   Usage Minimum (1)
    0x29, 0x05,  //   Usage Maximum (5)
    0x91, 0x02,  //   Output (Data, Variable, Absolute)
    0x95, 0x01,  //   Report Count (1)
    0x75, 0x03,  //   Report Size (3)
    0x91, 0x01,  //   Output (Constant)
    0x95, 0x06,  //   Report Count (6)
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Minimum (0)
    0x25, 0x65,  //   Logical Maximum (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Minimum (0)
    0x29, 0x65,  //   Usage Maximum (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0         // End Collection
};

// Standard QEMU usb-mouse HID Report Descriptor with wheel (52 bytes).
constexpr uint8 kStandardMouseReportDesc[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x02,  // Usage (Mouse)
    0xA1, 0x01,  // Collection (Application)
    0x09, 0x01,  //   Usage (Pointer)
    0xA1, 0x00,  //   Collection (Physical)
    0x05, 0x09,  //     Usage Page (Button)
    0x19, 0x01,  //     Usage Minimum (1)
    0x29, 0x03,  //     Usage Maximum (3)
    0x15, 0x00,  //     Logical Minimum (0)
    0x25, 0x01,  //     Logical Maximum (1)
    0x95, 0x03,  //     Report Count (3)
    0x75, 0x01,  //     Report Size (1)
    0x81, 0x02,  //     Input (Data, Variable, Absolute)
    0x95, 0x01,  //     Report Count (1)
    0x75, 0x05,  //     Report Size (5)
    0x81, 0x01,  //     Input (Constant)
    0x05, 0x01,  //     Usage Page (Generic Desktop)
    0x09, 0x30,  //     Usage (X)
    0x09, 0x31,  //     Usage (Y)
    0x09, 0x38,  //     Usage (Wheel)
    0x15, 0x81,  //     Logical Minimum (-127)
    0x25, 0x7F,  //     Logical Maximum (127)
    0x75, 0x08,  //     Report Size (8)
    0x95, 0x03,  //     Report Count (3)
    0x81, 0x06,  //     Input (Data, Variable, Relative)
    0xC0,        //   End Collection
    0xC0         // End Collection
};

// QEMU usb-tablet HID Report Descriptor (Absolute X/Y 0..32767 + relative wheel).
constexpr uint8 kQemuTabletReportDesc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x03,        //     Usage Maximum (3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Constant)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0               // End Collection
};

TEST(ParseStandardKeyboardDescriptorAndReport) {
  HidReportLayout layout = ParseHidReportDescriptor(
      kStandardKeyboardReportDesc, sizeof(kStandardKeyboardReportDesc), 1, 1);
  ASSERT(HidDeviceType::kKeyboard, layout.device_type);
  ASSERT(8, layout.report_byte_length);

  // Left Shift (bit 1 = 0xE1) + 'A' (0x04) + Enter (0x28).
  const uint8 report_bytes[8] = {0x02, 0x00, 0x04, 0x28, 0x00, 0x00, 0x00, 0x00};
  ParsedKeyboardReport parsed = ParseKeyboardReport(layout, report_bytes, 8);
  ASSERT(true, parsed.valid);
  ASSERT(3, parsed.pressed_keycodes.size());
  ASSERT(static_cast<uint8>(KeyCode::LeftShift), parsed.pressed_keycodes[0]);
  ASSERT(static_cast<uint8>(KeyCode::A), parsed.pressed_keycodes[1]);
  ASSERT(static_cast<uint8>(KeyCode::Enter), parsed.pressed_keycodes[2]);
}

TEST(ParseStandardMouseDescriptorAndReport) {
  HidReportLayout layout = ParseHidReportDescriptor(
      kStandardMouseReportDesc, sizeof(kStandardMouseReportDesc), 1, 2);
  ASSERT(HidDeviceType::kMouse, layout.device_type);
  ASSERT(true, layout.x_field.is_relative);
  ASSERT(true, layout.y_field.is_relative);
  ASSERT(true, layout.wheel_field.present);

  // Left + Right buttons (0x03), delta_x = -10 (0xF6), delta_y = +25 (0x19),
  // wheel = -1 (0xFF).
  const uint8 report_bytes[4] = {0x03, 0xF6, 0x19, 0xFF};
  ParsedPointerReport parsed = ParsePointerReport(layout, report_bytes, 4);
  ASSERT(true, parsed.valid);
  ASSERT(true, parsed.buttons[0]);
  ASSERT(true, parsed.buttons[1]);
  ASSERT(false, parsed.buttons[2]);
  ASSERT(-10, parsed.x);
  ASSERT(25, parsed.y);
  ASSERT(-1, parsed.wheel);
}

TEST(ParseQemuTabletDescriptorAndReport) {
  HidReportLayout layout = ParseHidReportDescriptor(
      kQemuTabletReportDesc, sizeof(kQemuTabletReportDesc), 0, 0);
  ASSERT(HidDeviceType::kTablet, layout.device_type);
  ASSERT(false, layout.x_field.is_relative);
  ASSERT(false, layout.y_field.is_relative);
  ASSERT(32767, layout.x_field.logical_max);
  ASSERT(32767, layout.y_field.logical_max);

  // Left button (0x01), X = 16384 (0x00, 0x40), Y = 32767 (0xFF, 0x7F),
  // Wheel = 2 (0x02).
  const uint8 report_bytes[6] = {0x01, 0x00, 0x40, 0xFF, 0x7F, 0x02};
  ParsedPointerReport parsed = ParsePointerReport(layout, report_bytes, 6);
  ASSERT(true, parsed.valid);
  ASSERT(true, parsed.buttons[0]);
  ASSERT(16384, parsed.x);
  ASSERT(32767, parsed.y);
  ASSERT(2, parsed.wheel);
  ASSERT_APPROX(0.5f, parsed.normalized_x, 0.01f);
  ASSERT_APPROX(1.0f, parsed.normalized_y, 0.01f);
}

}  // namespace
