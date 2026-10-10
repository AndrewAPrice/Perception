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

#include "usb_keyboard_device.h"

#include <algorithm>

#include "perception/services.h"
#include "perception/ui/keyboard.h"
#include "perception/window/window_manager.h"

using ::perception::FindFirstInstanceOfService;
using ::perception::devices::KeyboardEvent;
using ::perception::devices::KeyboardListener;
using ::perception::devices::UsbEndpointRingInfo;
using ::perception::ui::KeyCode;
using ::perception::window::WindowManager;

UsbKeyboardDevice::UsbKeyboardDevice(
    uint32 device_handle, const HidReportLayout& layout,
    const UsbEndpointRingInfo& ring_info, uint16 max_packet_size,
    std::function<void()> on_disconnect)
    : KeyboardDevice::Server({.defer_registration = true}),
      device_handle_(device_handle),
      layout_(layout),
      endpoint_(
          ring_info, max_packet_size,
          [this](const uint8* data, size_t len) { HandleReport(data, len); },
          std::move(on_disconnect)) {
  endpoint_.Enable();
  StartServing();
}

UsbKeyboardDevice::~UsbKeyboardDevice() {
  endpoint_.Disable();
  if (keyboard_captor_) keyboard_captor_->KeyboardReleased(nullptr);
}

Status UsbKeyboardDevice::SetKeyboardListener(
    const KeyboardListener::Client& listener) {
  if (keyboard_captor_) keyboard_captor_->KeyboardReleased(nullptr);
  if (listener.IsValid()) {
    keyboard_captor_ = std::make_unique<KeyboardListener::Client>(listener);
    keyboard_captor_->KeyboardTakenCaptive(nullptr);
  } else {
    keyboard_captor_.reset();
  }
  return Status::OK;
}

void UsbKeyboardDevice::HandleReport(const uint8* data, size_t length) {
  ParsedKeyboardReport report = ParseKeyboardReport(layout_, data, length);
  if (!report.valid) return;

  // Send KeyUp for any keys that were previously pressed but are now released.
  for (uint8 old_key : pressed_keys_) {
    if (std::find(report.pressed_keycodes.begin(),
                  report.pressed_keycodes.end(),
                  old_key) == report.pressed_keycodes.end()) {
      if (keyboard_captor_) {
        KeyboardEvent ev;
        ev.key = old_key;
        keyboard_captor_->KeyUp(ev, nullptr);
      }
    }
  }

  // Send KeyDown for any newly pressed keys.
  for (uint8 new_key : report.pressed_keycodes) {
    if (std::find(pressed_keys_.begin(), pressed_keys_.end(), new_key) ==
        pressed_keys_.end()) {
      if (new_key == static_cast<uint8>(KeyCode::Escape)) {
        auto window_manager = FindFirstInstanceOfService<WindowManager>();
        if (window_manager) window_manager->SystemButtonPushed(nullptr);
      }
      if (keyboard_captor_) {
        KeyboardEvent ev;
        ev.key = new_key;
        keyboard_captor_->KeyDown(ev, nullptr);
      }
    }
  }

  pressed_keys_ = std::move(report.pressed_keycodes);
}
