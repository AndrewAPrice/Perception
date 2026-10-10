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

#include <functional>
#include <memory>

#include "hid_parser.h"
#include "perception/devices/tablet_device.h"
#include "perception/devices/tablet_listener.h"
#include "perception/devices/usb_device.h"
#include "status.h"
#include "types.h"
#include "usb_mouse_device.h"
#include "usb_ring_endpoint.h"

// Serves an absolute-coordinate USB HID tablet as a TabletDevice.
class UsbTabletDevice : public ::perception::devices::TabletDevice::Server {
 public:
  UsbTabletDevice(uint32 device_handle, const HidReportLayout& layout,
                  const ::perception::devices::UsbEndpointRingInfo& ring_info,
                  uint16 max_packet_size, std::function<void()> on_disconnect);
  virtual ~UsbTabletDevice();

  uint32 device_handle() const { return device_handle_; }

  virtual Status SetTabletListener(
      const ::perception::devices::TabletListener::Client& listener) override;

  virtual Status SetMouseCaptured(
      const ::perception::devices::MouseCaptureState& state) override;

  void SetMouseDevice(std::shared_ptr<UsbMouseDevice> mouse_device);

  bool is_captured() const { return is_captured_; }

  void EnableDevice();
  void DisableDevice();

 private:
  uint32 device_handle_;
  HidReportLayout layout_;
  bool is_captured_ = false;
  float last_x_ = -1.0f;
  float last_y_ = -1.0f;
  bool last_buttons_[3] = {false, false, false};

  std::unique_ptr<::perception::devices::TabletListener::Client>
      tablet_listener_;
  std::shared_ptr<UsbMouseDevice> mouse_device_;
  UsbRingEndpoint endpoint_;

  void HandleReport(const uint8* data, size_t length);
};
