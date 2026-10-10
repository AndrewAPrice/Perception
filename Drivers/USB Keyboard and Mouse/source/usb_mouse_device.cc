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

#include "usb_mouse_device.h"

using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseListener;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::devices::UsbEndpointRingInfo;

namespace {

// Maps HID button index (0=Left, 1=Right, 2=Middle) to MouseButton enum.
MouseButton ButtonFromIndex(int index) {
  switch (index) {
    case 0:
      return MouseButton::Left;
    case 1:
      return MouseButton::Right;
    case 2:
      return MouseButton::Middle;
    default:
      return MouseButton::Unknown;
  }
}

}  // namespace

UsbMouseDevice::UsbMouseDevice(
    uint32 device_handle, const HidReportLayout& layout,
    const UsbEndpointRingInfo& ring_info, uint16 max_packet_size,
    std::function<void()> on_disconnect)
    : MouseDevice::Server({.defer_registration = true}),
      device_handle_(device_handle),
      layout_(layout),
      endpoint_(
          ring_info, max_packet_size,
          [this](const uint8* data, size_t len) { HandleReport(data, len); },
          std::move(on_disconnect)) {
  StartServing();
}

UsbMouseDevice::~UsbMouseDevice() {
  endpoint_.Disable();
  if (mouse_captor_) mouse_captor_->MouseReleased(nullptr);
}

Status UsbMouseDevice::SetMouseListener(const MouseListener::Client& listener) {
  if (mouse_captor_) mouse_captor_->MouseReleased(nullptr);
  if (listener.IsValid()) {
    mouse_captor_ = std::make_unique<MouseListener::Client>(listener);
    mouse_captor_->MouseTakenCaptive(nullptr);
  } else {
    mouse_captor_.reset();
  }
  return Status::OK;
}

void UsbMouseDevice::EnableDevice() { endpoint_.Enable(); }

void UsbMouseDevice::DisableDevice() { endpoint_.Disable(); }

void UsbMouseDevice::HandleReport(const uint8* data, size_t length) {
  ParsedPointerReport report = ParsePointerReport(layout_, data, length);
  if (!report.valid) return;

  if ((report.x != 0 || report.y != 0) && mouse_captor_) {
    RelativeMousePositionEvent move_ev;
    move_ev.delta_x = static_cast<float>(report.x);
    move_ev.delta_y = static_cast<float>(report.y);
    mouse_captor_->MouseMove(move_ev, nullptr);
  }

  if ((report.wheel != 0 || report.hwheel != 0) && mouse_captor_) {
    RelativeMousePositionEvent scroll_ev;
    scroll_ev.delta_x = static_cast<float>(report.hwheel);
    scroll_ev.delta_y = -static_cast<float>(report.wheel);
    mouse_captor_->MouseScroll(scroll_ev, nullptr);
  }

  for (int i = 0; i < 3; ++i) {
    if (report.buttons[i] != last_buttons_[i]) {
      last_buttons_[i] = report.buttons[i];
      if (mouse_captor_) {
        MouseButtonEvent btn_ev;
        btn_ev.button = ButtonFromIndex(i);
        btn_ev.is_pressed_down = report.buttons[i];
        mouse_captor_->MouseButton(btn_ev, nullptr);
      }
    }
  }
}
