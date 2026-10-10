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

#include "usb_tablet_device.h"

using ::perception::devices::MouseButton;
using ::perception::devices::MouseButtonEvent;
using ::perception::devices::MouseCaptureState;
using ::perception::devices::RelativeMousePositionEvent;
using ::perception::devices::TabletHoverEvent;
using ::perception::devices::TabletListener;
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

UsbTabletDevice::UsbTabletDevice(
    uint32 device_handle, const HidReportLayout& layout,
    const UsbEndpointRingInfo& ring_info, uint16 max_packet_size,
    std::function<void()> on_disconnect)
    : TabletDevice::Server({.defer_registration = true}),
      device_handle_(device_handle),
      layout_(layout),
      endpoint_(
          ring_info, max_packet_size,
          [this](const uint8* data, size_t len) { HandleReport(data, len); },
          std::move(on_disconnect)) {
  StartServing();
  EnableDevice();
}

UsbTabletDevice::~UsbTabletDevice() { endpoint_.Disable(); }

Status UsbTabletDevice::SetTabletListener(
    const TabletListener::Client& listener) {
  tablet_listener_ = listener.IsValid()
                         ? std::make_unique<TabletListener::Client>(listener)
                         : nullptr;
  return Status::OK;
}

Status UsbTabletDevice::SetMouseCaptured(const MouseCaptureState& state) {
  is_captured_ = state.is_captured;
  if (is_captured_) {
    DisableDevice();
    if (mouse_device_) mouse_device_->EnableDevice();
  } else {
    if (mouse_device_) mouse_device_->DisableDevice();
    EnableDevice();
  }
  return Status::OK;
}

void UsbTabletDevice::SetMouseDevice(
    std::shared_ptr<UsbMouseDevice> mouse_device) {
  mouse_device_ = std::move(mouse_device);
  if (mouse_device_) {
    if (is_captured_)
      mouse_device_->EnableDevice();
    else
      mouse_device_->DisableDevice();
  }
}

void UsbTabletDevice::EnableDevice() { endpoint_.Enable(); }

void UsbTabletDevice::DisableDevice() { endpoint_.Disable(); }

void UsbTabletDevice::HandleReport(const uint8* data, size_t length) {
  ParsedPointerReport report = ParsePointerReport(layout_, data, length);
  if (!report.valid) return;

  if (report.normalized_x != last_x_ || report.normalized_y != last_y_) {
    last_x_ = report.normalized_x;
    last_y_ = report.normalized_y;
    if (tablet_listener_) {
      TabletHoverEvent hover_ev;
      hover_ev.x = report.normalized_x;
      hover_ev.y = report.normalized_y;
      tablet_listener_->TabletHover(hover_ev, nullptr);
    }
  }

  if ((report.wheel != 0 || report.hwheel != 0) && tablet_listener_) {
    RelativeMousePositionEvent scroll_ev;
    scroll_ev.delta_x = static_cast<float>(report.hwheel);
    scroll_ev.delta_y = -static_cast<float>(report.wheel);
    tablet_listener_->TabletScroll(scroll_ev, nullptr);
  }

  for (int i = 0; i < 3; ++i) {
    if (report.buttons[i] != last_buttons_[i]) {
      last_buttons_[i] = report.buttons[i];
      if (tablet_listener_) {
        MouseButtonEvent btn_ev;
        btn_ev.button = ButtonFromIndex(i);
        btn_ev.is_pressed_down = report.buttons[i];
        tablet_listener_->TabletButton(btn_ev, nullptr);
      }
    }
  }
}
