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

#include "usb_drivers.h"

#include "driver_loader.h"

using ::perception::devices::UsbInterfaceInfo;

namespace {

// USB Class Code: Human Interface Device (HID).
constexpr uint8 kUsbClassHid = 0x03;

// USB Class Code: Hub.
constexpr uint8 kUsbClassHub = 0x09;

// USB HID SubClass Code: Boot Interface.
constexpr uint8 kUsbHidSubClassBoot = 1;

// USB HID Boot Protocol Code: Keyboard.
constexpr uint8 kUsbHidProtocolKeyboard = 1;

// USB HID Boot Protocol Code: Mouse.
constexpr uint8 kUsbHidProtocolMouse = 2;

// HID Item Type: Main.
constexpr uint8 kHidItemTypeMain = 0;

// HID Item Type: Global.
constexpr uint8 kHidItemTypeGlobal = 1;

// HID Item Type: Local.
constexpr uint8 kHidItemTypeLocal = 2;

// HID Main Tag: Collection.
constexpr uint8 kHidMainTagCollection = 0x0A;

// HID Collection Type: Application.
constexpr uint32 kHidCollectionApplication = 0x01;

// HID Global Tag: Usage Page.
constexpr uint8 kHidGlobalTagUsagePage = 0x00;

// HID Local Tag: Usage.
constexpr uint8 kHidLocalTagUsage = 0x00;

// HID Usage Page: Generic Desktop Controls.
constexpr uint16 kUsagePageGenericDesktop = 0x01;

// HID Usage Page: Digitizers.
constexpr uint16 kUsagePageDigitizer = 0x0D;

// HID Generic Desktop Usage: Pointer.
constexpr uint16 kUsageDesktopPointer = 0x01;

// HID Generic Desktop Usage: Mouse.
constexpr uint16 kUsageDesktopMouse = 0x02;

// HID Generic Desktop Usage: Keyboard.
constexpr uint16 kUsageDesktopKeyboard = 0x06;

// HID Generic Desktop Usage: Keypad.
constexpr uint16 kUsageDesktopKeypad = 0x07;

// HID Digitizer Usage: Digitizer.
constexpr uint16 kUsageDigitizer = 0x01;

// HID Digitizer Usage: Pen.
constexpr uint16 kUsageDigitizerPen = 0x02;

// HID Digitizer Usage: Touch Screen.
constexpr uint16 kUsageDigitizerTouchScreen = 0x04;

// HID Digitizer Usage: Touch Pad.
constexpr uint16 kUsageDigitizerTouchPad = 0x05;

// Driver executable name for USB HID keyboards, mice, and tablets.
constexpr std::string_view kUsbHidDriverName = "USB Keyboard and Mouse";

enum class DetectedHidCategory {
  kUnknown,
  kKeyboard,
  kPointing,
};

uint32 ReadUnsignedHidPayload(const uint8* data, uint8 size) {
  switch (size) {
    case 1:
      return data[0];
    case 2:
      return static_cast<uint32>(data[0]) | (static_cast<uint32>(data[1]) << 8);
    case 4:
      return static_cast<uint32>(data[0]) |
             (static_cast<uint32>(data[1]) << 8) |
             (static_cast<uint32>(data[2]) << 16) |
             (static_cast<uint32>(data[3]) << 24);
    default:
      return 0;
  }
}

DetectedHidCategory ClassifyHidUsage(uint16 usage_page, uint16 usage) {
  if (usage_page == kUsagePageGenericDesktop) {
    if (usage == kUsageDesktopKeyboard || usage == kUsageDesktopKeypad)
      return DetectedHidCategory::kKeyboard;
    if (usage == kUsageDesktopMouse || usage == kUsageDesktopPointer)
      return DetectedHidCategory::kPointing;
  } else if (usage_page == kUsagePageDigitizer) {
    if (usage == kUsageDigitizer || usage == kUsageDigitizerPen ||
        usage == kUsageDigitizerTouchScreen || usage == kUsageDigitizerTouchPad)
      return DetectedHidCategory::kPointing;
  }
  return DetectedHidCategory::kUnknown;
}

DetectedHidCategory ClassifyHidInterface(const UsbInterfaceInfo& iface) {
  const auto* data =
      reinterpret_cast<const uint8*>(iface.hid_report_descriptor.data());
  size_t length = iface.hid_report_descriptor.size();

  uint16 current_usage_page = 0;
  uint16 local_usage = 0;
  size_t offset = 0;

  while (offset < length) {
    uint8 prefix = data[offset++];
    if (prefix == 0xFE) {
      if (offset + 2 > length) break;
      uint8 data_size = data[offset];
      offset += 2 + data_size;
      continue;
    }

    uint8 size_code = prefix & 0x03;
    uint8 payload_size = (size_code == 3) ? 4 : size_code;
    uint8 item_type = (prefix >> 2) & 0x03;
    uint8 item_tag = (prefix >> 4) & 0x0F;
    if (offset + payload_size > length) break;

    uint32 uval = ReadUnsignedHidPayload(data + offset, payload_size);
    offset += payload_size;

    if (item_type == kHidItemTypeGlobal &&
        item_tag == kHidGlobalTagUsagePage) {
      current_usage_page = static_cast<uint16>(uval);
    } else if (item_type == kHidItemTypeLocal &&
               item_tag == kHidLocalTagUsage) {
      if (local_usage == 0) local_usage = static_cast<uint16>(uval);
    } else if (item_type == kHidItemTypeMain) {
      if (item_tag == kHidMainTagCollection &&
          (uval & 0xFF) == kHidCollectionApplication) {
        DetectedHidCategory cat =
            ClassifyHidUsage(current_usage_page, local_usage);
        if (cat != DetectedHidCategory::kUnknown) return cat;
      }
      local_usage = 0;
    }
  }

  if (iface.interface_subclass == kUsbHidSubClassBoot) {
    if (iface.interface_protocol == kUsbHidProtocolKeyboard)
      return DetectedHidCategory::kKeyboard;
    if (iface.interface_protocol == kUsbHidProtocolMouse)
      return DetectedHidCategory::kPointing;
  }

  return DetectedHidCategory::kUnknown;
}

}  // namespace

bool LoadUsbDriver(const UsbInterfaceInfo& interface_info,
                   bool handled_by_running_driver) {
  switch (interface_info.interface_class) {
    case kUsbClassHub:
      return true;
    case kUsbClassHid: {
      DetectedHidCategory category = ClassifyHidInterface(interface_info);
      if (category == DetectedHidCategory::kKeyboard) {
        FoundKeyboardDevice();
        if (!handled_by_running_driver) RequestDriverLoad(kUsbHidDriverName);
        return true;
      }
      if (category == DetectedHidCategory::kPointing) {
        FoundPointingDevice();
        if (!handled_by_running_driver) RequestDriverLoad(kUsbHidDriverName);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}
