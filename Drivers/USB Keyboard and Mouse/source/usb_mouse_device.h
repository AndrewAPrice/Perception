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
#include "perception/devices/mouse_device.h"
#include "perception/devices/mouse_listener.h"
#include "perception/devices/usb_device.h"
#include "status.h"
#include "types.h"
#include "usb_ring_endpoint.h"

// Serves a relative USB HID mouse as a perception::devices::MouseDevice.
class UsbMouseDevice : public ::perception::devices::MouseDevice::Server {
 public:
  UsbMouseDevice(uint32 device_handle, const HidReportLayout& layout,
                 const ::perception::devices::UsbEndpointRingInfo& ring_info,
                 uint16 max_packet_size, std::function<void()> on_disconnect);
  virtual ~UsbMouseDevice();

  uint32 device_handle() const { return device_handle_; }

  virtual Status SetMouseListener(
      const ::perception::devices::MouseListener::Client& listener) override;

  // Starts queuing Interrupt IN TRBs for the relative mouse.
  void EnableDevice();

  // Stops queuing new Interrupt IN TRBs for the relative mouse.
  void DisableDevice();

 private:
  uint32 device_handle_;
  HidReportLayout layout_;
  bool last_buttons_[3] = {false, false, false};
  std::unique_ptr<::perception::devices::MouseListener::Client> mouse_captor_;
  UsbRingEndpoint endpoint_;

  void HandleReport(const uint8* data, size_t length);
};
