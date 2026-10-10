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

#include <algorithm>
#include <map>

#include "perception/ui/keyboard.h"

using ::perception::ui::KeyCode;

namespace {

// HID Item Type: Main.
constexpr uint8 kHidItemTypeMain = 0;

// HID Item Type: Global.
constexpr uint8 kHidItemTypeGlobal = 1;

// HID Item Type: Local.
constexpr uint8 kHidItemTypeLocal = 2;

// HID Main Item Tag: Input.
constexpr uint8 kHidMainTagInput = 8;

// HID Main Item Tag: Collection.
constexpr uint8 kHidMainTagCollection = 10;

// HID Main Item Tag: End Collection.
constexpr uint8 kHidMainTagEndCollection = 12;

// HID Global Item Tag: Usage Page.
constexpr uint8 kHidGlobalTagUsagePage = 0;

// HID Global Item Tag: Logical Minimum.
constexpr uint8 kHidGlobalTagLogicalMin = 1;

// HID Global Item Tag: Logical Maximum.
constexpr uint8 kHidGlobalTagLogicalMax = 2;

// HID Global Item Tag: Report Size.
constexpr uint8 kHidGlobalTagReportSize = 7;

// HID Global Item Tag: Report ID.
constexpr uint8 kHidGlobalTagReportId = 8;

// HID Global Item Tag: Report Count.
constexpr uint8 kHidGlobalTagReportCount = 9;

// HID Global Item Tag: Push.
constexpr uint8 kHidGlobalTagPush = 10;

// HID Global Item Tag: Pop.
constexpr uint8 kHidGlobalTagPop = 11;

// HID Local Item Tag: Usage.
constexpr uint8 kHidLocalTagUsage = 0;

// HID Local Item Tag: Usage Minimum.
constexpr uint8 kHidLocalTagUsageMin = 1;

// HID Local Item Tag: Usage Maximum.
constexpr uint8 kHidLocalTagUsageMax = 2;

// HID Input Flag: Constant (bit 0).
constexpr uint32 kHidInputFlagConstant = 1U << 0;

// HID Input Flag: Variable (bit 1).
constexpr uint32 kHidInputFlagVariable = 1U << 1;

// HID Input Flag: Relative (bit 2).
constexpr uint32 kHidInputFlagRelative = 1U << 2;

// HID Usage Page: Generic Desktop.
constexpr uint16 kUsagePageGenericDesktop = 0x01;

// HID Usage Page: Keyboard / Keypad.
constexpr uint16 kUsagePageKeyboard = 0x07;

// HID Usage Page: Button.
constexpr uint16 kUsagePageButton = 0x09;

// HID Usage Page: Consumer.
constexpr uint16 kUsagePageConsumer = 0x0C;

// HID Usage Page: Digitizers.
constexpr uint16 kUsagePageDigitizer = 0x0D;

// Generic Desktop Usage: Pointer.
constexpr uint16 kDesktopUsagePointer = 0x01;

// Generic Desktop Usage: Mouse.
constexpr uint16 kDesktopUsageMouse = 0x02;

// Generic Desktop Usage: Keyboard.
constexpr uint16 kDesktopUsageKeyboard = 0x06;

// Generic Desktop Usage: Keypad.
constexpr uint16 kDesktopUsageKeypad = 0x07;

// Generic Desktop Usage: X Axis.
constexpr uint16 kDesktopUsageX = 0x30;

// Generic Desktop Usage: Y Axis.
constexpr uint16 kDesktopUsageY = 0x31;

// Generic Desktop Usage: Wheel.
constexpr uint16 kDesktopUsageWheel = 0x38;

// Consumer Usage: AC Pan (Horizontal Wheel).
constexpr uint16 kConsumerUsageAcPan = 0x0238;

// Digitizer Usage: Tip Switch.
constexpr uint16 kDigitizerUsageTipSwitch = 0x42;

// First valid non-reserved USB HID Keyboard usage ID ('A' = 0x04).
constexpr uint8 kFirstValidHidKeyUsage = 0x04;

// Builds a standard 8-byte Boot Protocol Keyboard report layout.
HidReportLayout MakeBootKeyboardLayout() {
  HidReportLayout layout;
  layout.device_type = HidDeviceType::kKeyboard;
  layout.is_boot_protocol_fallback = true;
  layout.report_byte_length = 8;
  for (uint8 i = 0; i < 8; ++i) {
    layout.key_bit_fields.push_back(
        {.bit_offset = i, .usage = static_cast<uint16>(0xE0 + i)});
  }
  layout.key_array_fields.push_back(
      {.bit_offset = 16, .bit_size = 8, .count = 6, .usage_min = 0});
  return layout;
}

// Builds a standard 3-to-4-byte Boot Protocol Mouse report layout.
HidReportLayout MakeBootMouseLayout() {
  HidReportLayout layout;
  layout.device_type = HidDeviceType::kMouse;
  layout.is_boot_protocol_fallback = true;
  layout.report_byte_length = 3;
  for (int i = 0; i < 3; ++i) {
    layout.button_present[i] = true;
    layout.button_bit_offset[i] = i;
  }
  layout.x_field = {.present = true,
                    .bit_offset = 8,
                    .bit_size = 8,
                    .is_signed = true,
                    .is_relative = true,
                    .logical_min = -127,
                    .logical_max = 127};
  layout.y_field = {.present = true,
                    .bit_offset = 16,
                    .bit_size = 8,
                    .is_signed = true,
                    .is_relative = true,
                    .logical_min = -127,
                    .logical_max = 127};
  layout.wheel_field = {.present = true,
                        .bit_offset = 24,
                        .bit_size = 8,
                        .is_signed = true,
                        .is_relative = true,
                        .logical_min = -127,
                        .logical_max = 127};
  return layout;
}

struct GlobalState {
  uint16 usage_page = 0;
  int32 logical_min = 0;
  int32 logical_max = 0;
  uint32 report_size = 0;
  uint32 report_count = 0;
  uint8 report_id = 0;
};

struct LocalState {
  std::vector<uint16> usages;
  bool has_usage_range = false;
  uint16 usage_min = 0;
  uint16 usage_max = 0;
};

int32 SignExtendItem(uint32 raw, uint8 byte_count) {
  if (byte_count == 1) return static_cast<int8>(raw & 0xFF);
  if (byte_count == 2) return static_cast<int16>(raw & 0xFFFF);
  return static_cast<int32>(raw);
}

void AddUniqueKeyCode(std::vector<uint8>& list, uint8 keycode) {
  if (keycode == 0) return;
  if (std::find(list.begin(), list.end(), keycode) == list.end())
    list.push_back(keycode);
}

}  // namespace

HidReportLayout ParseHidReportDescriptor(const uint8* data, size_t length,
                                         uint8 interface_subclass,
                                         uint8 interface_protocol) {
  HidReportLayout layout;

  if (!data || length == 0) {
    if (interface_subclass == 1 && interface_protocol == 1)
      return MakeBootKeyboardLayout();
    if (interface_subclass == 1 && interface_protocol == 2)
      return MakeBootMouseLayout();
    return layout;
  }

  GlobalState global;
  std::vector<GlobalState> global_stack;
  LocalState local;
  std::map<uint8, uint32> bit_offset_by_report_id;
  bool target_report_id_locked = false;

  size_t pos = 0;
  while (pos < length) {
    uint8 prefix = data[pos++];
    if (prefix == 0xFE) {
      // Long item: skip data_size + long_tag bytes.
      if (pos + 2 > length) break;
      uint8 data_size = data[pos];
      pos += 2 + data_size;
      continue;
    }

    uint8 size_code = prefix & 0x03;
    uint8 byte_count = (size_code == 3) ? 4 : size_code;
    uint8 item_type = (prefix >> 2) & 0x03;
    uint8 item_tag = (prefix >> 4) & 0x0F;

    if (pos + byte_count > length) break;

    uint32 uval = 0;
    for (uint8 i = 0; i < byte_count; ++i)
      uval |= (static_cast<uint32>(data[pos + i]) << (i * 8));
    pos += byte_count;

    if (item_type == kHidItemTypeGlobal) {
      switch (item_tag) {
        case kHidGlobalTagUsagePage:
          global.usage_page = static_cast<uint16>(uval);
          break;
        case kHidGlobalTagLogicalMin:
          global.logical_min = SignExtendItem(uval, byte_count);
          break;
        case kHidGlobalTagLogicalMax:
          global.logical_max = (global.logical_min < 0 || byte_count == 4)
                                   ? SignExtendItem(uval, byte_count)
                                   : static_cast<int32>(uval);
          break;
        case kHidGlobalTagReportSize:
          global.report_size = uval;
          break;
        case kHidGlobalTagReportId:
          global.report_id = static_cast<uint8>(uval & 0xFF);
          layout.uses_report_id = true;
          if (!target_report_id_locked) layout.report_id = global.report_id;
          break;
        case kHidGlobalTagReportCount:
          global.report_count = uval;
          break;
        case kHidGlobalTagPush:
          global_stack.push_back(global);
          break;
        case kHidGlobalTagPop:
          if (!global_stack.empty()) {
            global = global_stack.back();
            global_stack.pop_back();
          }
          break;
        default:
          break;
      }
    } else if (item_type == kHidItemTypeLocal) {
      uint16 usage16 = static_cast<uint16>(uval & 0xFFFF);
      switch (item_tag) {
        case kHidLocalTagUsage:
          local.usages.push_back(usage16);
          break;
        case kHidLocalTagUsageMin:
          local.has_usage_range = true;
          local.usage_min = usage16;
          break;
        case kHidLocalTagUsageMax:
          local.has_usage_range = true;
          local.usage_max = usage16;
          break;
        default:
          break;
      }
    } else if (item_type == kHidItemTypeMain) {
      if (item_tag == kHidMainTagCollection) {
        uint16 col_usage = local.usages.empty() ? 0 : local.usages.front();
        if (layout.device_type == HidDeviceType::kUnknown) {
          if (global.usage_page == kUsagePageGenericDesktop) {
            if (col_usage == kDesktopUsageKeyboard ||
                col_usage == kDesktopUsageKeypad) {
              layout.device_type = HidDeviceType::kKeyboard;
            } else if (col_usage == kDesktopUsageMouse ||
                       col_usage == kDesktopUsagePointer) {
              layout.device_type = HidDeviceType::kMouse;
            }
          } else if (global.usage_page == kUsagePageDigitizer) {
            layout.device_type = HidDeviceType::kTablet;
          }
        }
      } else if (item_tag == kHidMainTagInput) {
        uint32& cur_bit_offset = bit_offset_by_report_id[global.report_id];
        bool is_constant = (uval & kHidInputFlagConstant) != 0;
        bool is_variable = (uval & kHidInputFlagVariable) != 0;
        bool is_relative = (uval & kHidInputFlagRelative) != 0;
        uint32 total_bits = global.report_size * global.report_count;

        bool matches_report_id =
            !target_report_id_locked || (global.report_id == layout.report_id);

        if (!is_constant && global.report_size > 0 &&
            global.report_count > 0 && matches_report_id) {
          if (global.usage_page == kUsagePageKeyboard) {
            target_report_id_locked = true;
            layout.report_id = global.report_id;
            if (layout.device_type == HidDeviceType::kUnknown)
              layout.device_type = HidDeviceType::kKeyboard;

            if (is_variable && global.report_size == 1) {
              for (uint32 i = 0; i < global.report_count; ++i) {
                uint16 usage = 0;
                if (local.has_usage_range) {
                  usage = std::min<uint16>(
                      static_cast<uint16>(local.usage_min + i),
                      local.usage_max);
                } else if (i < local.usages.size()) {
                  usage = local.usages[i];
                }
                if (usage != 0) {
                  layout.key_bit_fields.push_back(
                      {.bit_offset = cur_bit_offset + i, .usage = usage});
                }
              }
            } else if (!is_variable && global.report_size <= 16) {
              layout.key_array_fields.push_back(
                  {.bit_offset = cur_bit_offset,
                   .bit_size = static_cast<uint8>(global.report_size),
                   .count = static_cast<uint8>(global.report_count),
                   .usage_min = static_cast<uint16>(
                       local.has_usage_range ? local.usage_min : 0)});
            }
          } else if (global.usage_page == kUsagePageButton) {
            target_report_id_locked = true;
            layout.report_id = global.report_id;
            for (uint32 i = 0; i < global.report_count; ++i) {
              uint16 btn_usage = 0;
              if (local.has_usage_range) {
                btn_usage = static_cast<uint16>(local.usage_min + i);
              } else if (i < local.usages.size()) {
                btn_usage = local.usages[i];
              }
              if (btn_usage >= 1 && btn_usage <= 3) {
                size_t btn_idx = btn_usage - 1;
                if (!layout.button_present[btn_idx]) {
                  layout.button_present[btn_idx] = true;
                  layout.button_bit_offset[btn_idx] =
                      cur_bit_offset + i * global.report_size;
                }
              }
            }
          } else if (global.usage_page == kUsagePageDigitizer) {
            target_report_id_locked = true;
            layout.report_id = global.report_id;
            for (uint32 i = 0; i < global.report_count; ++i) {
              uint16 dig_usage = 0;
              if (local.has_usage_range) {
                dig_usage = static_cast<uint16>(local.usage_min + i);
              } else if (i < local.usages.size()) {
                dig_usage = local.usages[i];
              }
              if (dig_usage == kDigitizerUsageTipSwitch &&
                  !layout.button_present[0]) {
                layout.button_present[0] = true;
                layout.button_bit_offset[0] =
                    cur_bit_offset + i * global.report_size;
              }
            }
          } else if (global.usage_page == kUsagePageGenericDesktop ||
                     global.usage_page == kUsagePageConsumer) {
            for (uint32 i = 0; i < global.report_count; ++i) {
              uint16 usage = 0;
              if (local.has_usage_range) {
                usage = static_cast<uint16>(local.usage_min + i);
              } else if (i < local.usages.size()) {
                usage = local.usages[i];
              } else if (!local.usages.empty()) {
                usage = local.usages.back();
              }

              uint32 field_offset = cur_bit_offset + i * global.report_size;
              HidAxisField axis = {
                  .present = true,
                  .bit_offset = field_offset,
                  .bit_size = static_cast<uint8>(global.report_size),
                  .is_signed = (global.logical_min < 0),
                  .is_relative = is_relative,
                  .logical_min = global.logical_min,
                  .logical_max = global.logical_max};

              if (global.usage_page == kUsagePageGenericDesktop &&
                  usage == kDesktopUsageX && !layout.x_field.present) {
                target_report_id_locked = true;
                layout.report_id = global.report_id;
                layout.x_field = axis;
                layout.device_type = is_relative ? HidDeviceType::kMouse
                                                 : HidDeviceType::kTablet;
              } else if (global.usage_page == kUsagePageGenericDesktop &&
                         usage == kDesktopUsageY && !layout.y_field.present) {
                target_report_id_locked = true;
                layout.report_id = global.report_id;
                layout.y_field = axis;
                layout.device_type = is_relative ? HidDeviceType::kMouse
                                                 : HidDeviceType::kTablet;
              } else if (global.usage_page == kUsagePageGenericDesktop &&
                         usage == kDesktopUsageWheel &&
                         !layout.wheel_field.present) {
                layout.wheel_field = axis;
              } else if (global.usage_page == kUsagePageConsumer &&
                         usage == kConsumerUsageAcPan &&
                         !layout.hwheel_field.present) {
                layout.hwheel_field = axis;
              }
            }
          }
        }

        cur_bit_offset += total_bits;
      }

      // Local state is cleared after every Main item.
      local = LocalState{};
    }
  }

  uint32 target_bits = bit_offset_by_report_id[layout.report_id];
  layout.report_byte_length =
      (target_bits + 7) / 8 + (layout.uses_report_id ? 1 : 0);

  if (layout.device_type == HidDeviceType::kUnknown) {
    if (interface_subclass == 1 && interface_protocol == 1)
      return MakeBootKeyboardLayout();
    if (interface_subclass == 1 && interface_protocol == 2)
      return MakeBootMouseLayout();
  }

  return layout;
}

int32 ExtractHidFieldValue(const uint8* report, size_t report_len,
                           uint32 bit_offset, uint8 bit_size, bool is_signed) {
  if (!report || bit_size == 0 || bit_size > 32) return 0;
  if ((bit_offset + bit_size + 7) / 8 > report_len) return 0;

  uint32 raw = 0;
  for (uint8 i = 0; i < bit_size; ++i) {
    uint32 bit_pos = bit_offset + i;
    uint8 byte_val = report[bit_pos / 8];
    if (byte_val & (1U << (bit_pos % 8))) raw |= (1U << i);
  }

  if (is_signed && bit_size < 32) {
    uint32 sign_bit = 1U << (bit_size - 1);
    if (raw & sign_bit) raw |= (~0U << bit_size);
  }
  return static_cast<int32>(raw);
}

ParsedKeyboardReport ParseKeyboardReport(const HidReportLayout& layout,
                                         const uint8* report,
                                         size_t report_len) {
  ParsedKeyboardReport result;
  if (!report || report_len == 0 ||
      layout.device_type != HidDeviceType::kKeyboard) {
    return result;
  }

  const uint8* payload = report;
  size_t payload_len = report_len;
  if (layout.uses_report_id) {
    if (report[0] != layout.report_id || report_len < 2) return result;
    payload = report + 1;
    payload_len = report_len - 1;
  }

  result.valid = true;

  for (const auto& bit_field : layout.key_bit_fields) {
    if ((bit_field.bit_offset / 8) < payload_len) {
      int32 bit_val = ExtractHidFieldValue(payload, payload_len,
                                           bit_field.bit_offset, 1, false);
      if (bit_val != 0 && bit_field.usage <= 0xFF) {
        AddUniqueKeyCode(result.pressed_keycodes,
                         HidUsageToKeyCode(static_cast<uint8>(bit_field.usage)));
      }
    }
  }

  for (const auto& arr : layout.key_array_fields) {
    for (uint8 i = 0; i < arr.count; ++i) {
      uint32 offset = arr.bit_offset + static_cast<uint32>(i) * arr.bit_size;
      if ((offset + arr.bit_size + 7) / 8 > payload_len) break;
      uint32 usage = static_cast<uint32>(
                         ExtractHidFieldValue(payload, payload_len, offset,
                                              arr.bit_size, false)) +
                     arr.usage_min;
      if (usage >= kFirstValidHidKeyUsage && usage <= 0xFF) {
        AddUniqueKeyCode(result.pressed_keycodes,
                         HidUsageToKeyCode(static_cast<uint8>(usage)));
      }
    }
  }

  return result;
}

ParsedPointerReport ParsePointerReport(const HidReportLayout& layout,
                                       const uint8* report, size_t report_len) {
  ParsedPointerReport result;
  if (!report || report_len == 0 ||
      (layout.device_type != HidDeviceType::kMouse &&
       layout.device_type != HidDeviceType::kTablet)) {
    return result;
  }

  const uint8* payload = report;
  size_t payload_len = report_len;
  if (layout.uses_report_id) {
    if (report[0] != layout.report_id || report_len < 2) return result;
    payload = report + 1;
    payload_len = report_len - 1;
  }

  result.valid = true;

  for (int i = 0; i < 3; ++i) {
    if (layout.button_present[i]) {
      result.buttons[i] = ExtractHidFieldValue(payload, payload_len,
                                               layout.button_bit_offset[i], 1,
                                               false) != 0;
    }
  }

  if (layout.x_field.present) {
    result.x = ExtractHidFieldValue(
        payload, payload_len, layout.x_field.bit_offset,
        layout.x_field.bit_size, layout.x_field.is_signed);
    if (!layout.x_field.is_relative &&
        layout.x_field.logical_max > layout.x_field.logical_min) {
      float norm =
          static_cast<float>(result.x - layout.x_field.logical_min) /
          static_cast<float>(layout.x_field.logical_max -
                             layout.x_field.logical_min);
      result.normalized_x = std::clamp(norm, 0.0f, 1.0f);
    }
  }

  if (layout.y_field.present) {
    result.y = ExtractHidFieldValue(
        payload, payload_len, layout.y_field.bit_offset,
        layout.y_field.bit_size, layout.y_field.is_signed);
    if (!layout.y_field.is_relative &&
        layout.y_field.logical_max > layout.y_field.logical_min) {
      float norm =
          static_cast<float>(result.y - layout.y_field.logical_min) /
          static_cast<float>(layout.y_field.logical_max -
                             layout.y_field.logical_min);
      result.normalized_y = std::clamp(norm, 0.0f, 1.0f);
    }
  }

  if (layout.wheel_field.present &&
      (layout.wheel_field.bit_offset + layout.wheel_field.bit_size + 7) / 8 <=
          payload_len) {
    result.wheel = ExtractHidFieldValue(
        payload, payload_len, layout.wheel_field.bit_offset,
        layout.wheel_field.bit_size, layout.wheel_field.is_signed);
  }

  if (layout.hwheel_field.present &&
      (layout.hwheel_field.bit_offset + layout.hwheel_field.bit_size + 7) / 8 <=
          payload_len) {
    result.hwheel = ExtractHidFieldValue(
        payload, payload_len, layout.hwheel_field.bit_offset,
        layout.hwheel_field.bit_size, layout.hwheel_field.is_signed);
  }

  return result;
}

uint8 HidUsageToKeyCode(uint8 hid_usage) {
  switch (hid_usage) {
    case 0x04:
      return static_cast<uint8>(KeyCode::A);
    case 0x05:
      return static_cast<uint8>(KeyCode::B);
    case 0x06:
      return static_cast<uint8>(KeyCode::C);
    case 0x07:
      return static_cast<uint8>(KeyCode::D);
    case 0x08:
      return static_cast<uint8>(KeyCode::E);
    case 0x09:
      return static_cast<uint8>(KeyCode::F);
    case 0x0A:
      return static_cast<uint8>(KeyCode::G);
    case 0x0B:
      return static_cast<uint8>(KeyCode::H);
    case 0x0C:
      return static_cast<uint8>(KeyCode::I);
    case 0x0D:
      return static_cast<uint8>(KeyCode::J);
    case 0x0E:
      return static_cast<uint8>(KeyCode::K);
    case 0x0F:
      return static_cast<uint8>(KeyCode::L);
    case 0x10:
      return static_cast<uint8>(KeyCode::M);
    case 0x11:
      return static_cast<uint8>(KeyCode::N);
    case 0x12:
      return static_cast<uint8>(KeyCode::O);
    case 0x13:
      return static_cast<uint8>(KeyCode::P);
    case 0x14:
      return static_cast<uint8>(KeyCode::Q);
    case 0x15:
      return static_cast<uint8>(KeyCode::R);
    case 0x16:
      return static_cast<uint8>(KeyCode::S);
    case 0x17:
      return static_cast<uint8>(KeyCode::T);
    case 0x18:
      return static_cast<uint8>(KeyCode::U);
    case 0x19:
      return static_cast<uint8>(KeyCode::V);
    case 0x1A:
      return static_cast<uint8>(KeyCode::W);
    case 0x1B:
      return static_cast<uint8>(KeyCode::X);
    case 0x1C:
      return static_cast<uint8>(KeyCode::Y);
    case 0x1D:
      return static_cast<uint8>(KeyCode::Z);
    case 0x1E:
      return static_cast<uint8>(KeyCode::One);
    case 0x1F:
      return static_cast<uint8>(KeyCode::Two);
    case 0x20:
      return static_cast<uint8>(KeyCode::Three);
    case 0x21:
      return static_cast<uint8>(KeyCode::Four);
    case 0x22:
      return static_cast<uint8>(KeyCode::Five);
    case 0x23:
      return static_cast<uint8>(KeyCode::Six);
    case 0x24:
      return static_cast<uint8>(KeyCode::Seven);
    case 0x25:
      return static_cast<uint8>(KeyCode::Eight);
    case 0x26:
      return static_cast<uint8>(KeyCode::Nine);
    case 0x27:
      return static_cast<uint8>(KeyCode::Zero);
    case 0x28:
      return static_cast<uint8>(KeyCode::Enter);
    case 0x29:
      return static_cast<uint8>(KeyCode::Escape);
    case 0x2A:
      return static_cast<uint8>(KeyCode::Backspace);
    case 0x2B:
      return static_cast<uint8>(KeyCode::Tab);
    case 0x2C:
      return static_cast<uint8>(KeyCode::Space);
    case 0x2D:
      return static_cast<uint8>(KeyCode::Hyphen);
    case 0x2E:
      return static_cast<uint8>(KeyCode::Equals);
    case 0x2F:
      return static_cast<uint8>(KeyCode::LeftBracket);
    case 0x30:
      return static_cast<uint8>(KeyCode::RightBracket);
    case 0x31:
    case 0x32:
      return static_cast<uint8>(KeyCode::Backslash);
    case 0x33:
      return static_cast<uint8>(KeyCode::Semicolon);
    case 0x34:
      return static_cast<uint8>(KeyCode::Apostrophe);
    case 0x35:
      return static_cast<uint8>(KeyCode::Backtick);
    case 0x36:
      return static_cast<uint8>(KeyCode::Comma);
    case 0x37:
      return static_cast<uint8>(KeyCode::Period);
    case 0x38:
      return static_cast<uint8>(KeyCode::Slash);
    case 0x39:
      return static_cast<uint8>(KeyCode::CapsLock);
    case 0x3A:
      return static_cast<uint8>(KeyCode::F1);
    case 0x3B:
      return static_cast<uint8>(KeyCode::F2);
    case 0x3C:
      return static_cast<uint8>(KeyCode::F3);
    case 0x3D:
      return static_cast<uint8>(KeyCode::F4);
    case 0x3E:
      return static_cast<uint8>(KeyCode::F5);
    case 0x3F:
      return static_cast<uint8>(KeyCode::F6);
    case 0x40:
      return static_cast<uint8>(KeyCode::F7);
    case 0x41:
      return static_cast<uint8>(KeyCode::F8);
    case 0x42:
      return static_cast<uint8>(KeyCode::F9);
    case 0x43:
      return static_cast<uint8>(KeyCode::F10);
    case 0x44:
      return static_cast<uint8>(KeyCode::F11);
    case 0x45:
      return static_cast<uint8>(KeyCode::F12);
    case 0x47:
      return static_cast<uint8>(KeyCode::ScrollLock);
    case 0x49:
    case 0x62:
      return static_cast<uint8>(KeyCode::Insert);
    case 0x4A:
    case 0x5F:
      return static_cast<uint8>(KeyCode::Home);
    case 0x4B:
    case 0x61:
      return static_cast<uint8>(KeyCode::PageUp);
    case 0x4C:
    case 0x63:
      return static_cast<uint8>(KeyCode::Delete);
    case 0x4D:
    case 0x59:
      return static_cast<uint8>(KeyCode::End);
    case 0x4E:
    case 0x5B:
      return static_cast<uint8>(KeyCode::PageDown);
    case 0x4F:
    case 0x5E:
      return static_cast<uint8>(KeyCode::RightArrow);
    case 0x50:
    case 0x5C:
      return static_cast<uint8>(KeyCode::LeftArrow);
    case 0x51:
    case 0x5A:
      return static_cast<uint8>(KeyCode::DownArrow);
    case 0x52:
    case 0x60:
      return static_cast<uint8>(KeyCode::UpArrow);
    case 0x53:
      return static_cast<uint8>(KeyCode::NumLock);
    case 0x54:
      return static_cast<uint8>(KeyCode::Slash);
    case 0x55:
      return static_cast<uint8>(KeyCode::KeypadAsterisk);
    case 0x56:
      return static_cast<uint8>(KeyCode::KeypadMinus);
    case 0x57:
      return static_cast<uint8>(KeyCode::KeypadPlus);
    case 0x58:
      return static_cast<uint8>(KeyCode::Enter);
    case 0x5D:
      return static_cast<uint8>(KeyCode::Keypad5);
    case 0x65:
      return static_cast<uint8>(KeyCode::Menu);
    case 0xE0:
    case 0xE4:
      return static_cast<uint8>(KeyCode::LeftControl);
    case 0xE1:
      return static_cast<uint8>(KeyCode::LeftShift);
    case 0xE2:
    case 0xE6:
      return static_cast<uint8>(KeyCode::LeftAlt);
    case 0xE3:
      return static_cast<uint8>(KeyCode::LeftCommand);
    case 0xE5:
      return static_cast<uint8>(KeyCode::RightShift);
    case 0xE7:
      return static_cast<uint8>(KeyCode::RightCommand);
    default:
      return 0;
  }
}
