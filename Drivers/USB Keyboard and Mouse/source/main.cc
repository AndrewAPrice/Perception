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

#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "hid_parser.h"
#include "perception/devices/device_manager.h"
#include "perception/devices/usb_device.h"
#include "perception/processes.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "usb_keyboard_device.h"
#include "usb_mouse_device.h"
#include "usb_tablet_device.h"

using ::perception::Defer;
using ::perception::GetService;
using ::perception::HandOverControl;
using ::perception::IsDuplicateInstanceOfProcess;
using ::perception::devices::DeviceManager;
using ::perception::devices::OpenUsbEndpointRequest;
using ::perception::devices::UsbControlTransferRequest;
using ::perception::devices::UsbDeviceId;
using ::perception::devices::UsbDeviceListener;
using ::perception::devices::UsbInterfaceFilter;
using ::perception::devices::UsbInterfaceInfo;

namespace {

// USB Class Code: Human Interface Device (HID).
constexpr int16 kUsbClassHid = 0x03;

// USB HID Class Request: SET_IDLE.
constexpr uint8 kHidReqSetIdle = 0x0A;

// USB HID Class Request: SET_PROTOCOL.
constexpr uint8 kHidReqSetProtocol = 0x0B;

std::set<std::pair<uint32, uint8>> attached_interfaces;
std::vector<std::shared_ptr<UsbKeyboardDevice>> keyboards;
std::vector<std::shared_ptr<UsbMouseDevice>> mice;
std::vector<std::shared_ptr<UsbTabletDevice>> tablets;

void SyncTabletAndMousePairing() {
  if (!tablets.empty()) {
    std::shared_ptr<UsbMouseDevice> primary_mouse =
        mice.empty() ? nullptr : mice.front();
    tablets.front()->SetMouseDevice(primary_mouse);
    for (size_t i = 1; i < mice.size(); ++i) {
      if (tablets.front()->is_captured())
        mice[i]->EnableDevice();
      else
        mice[i]->DisableDevice();
    }
  } else {
    for (auto& mouse : mice) mouse->EnableDevice();
  }
}

void DetachDeviceByHandle(uint32 device_handle) {
  for (auto it = attached_interfaces.begin();
       it != attached_interfaces.end();) {
    if (it->first == device_handle)
      it = attached_interfaces.erase(it);
    else
      ++it;
  }

  std::erase_if(keyboards, [device_handle](const auto& kbd) {
    return kbd->device_handle() == device_handle;
  });
  std::erase_if(mice, [device_handle](const auto& mse) {
    return mse->device_handle() == device_handle;
  });
  std::erase_if(tablets, [device_handle](const auto& tab) {
    return tab->device_handle() == device_handle;
  });

  SyncTabletAndMousePairing();
}

void AttachHidInterface(const UsbInterfaceInfo& iface) {
  if (iface.interface_class != kUsbClassHid || iface.interrupt_in_dci == 0)
    return;

  auto key = std::make_pair(iface.device_handle, iface.interface_number);
  if (attached_interfaces.contains(key)) return;

  HidReportLayout layout = ParseHidReportDescriptor(
      reinterpret_cast<const uint8*>(iface.hid_report_descriptor.data()),
      iface.hid_report_descriptor.size(), iface.interface_subclass,
      iface.interface_protocol);

  if (layout.device_type == HidDeviceType::kUnknown) return;

  auto device_manager = GetService<DeviceManager>();

  // Send SET_IDLE(0) so the device only reports on state changes (or per its
  // interrupt interval for mice/tablets).
  UsbControlTransferRequest idle_req;
  idle_req.device_handle = iface.device_handle;
  idle_req.request_type = 0x21;
  idle_req.request = kHidReqSetIdle;
  idle_req.value = 0;
  idle_req.index = iface.interface_number;
  idle_req.length = 0;
  (void)device_manager.UsbControlTransfer(idle_req);

  // If the interface supports Boot Subclass, select Boot Protocol (0) only if
  // falling back to Boot Protocol, or Report Protocol (1) when using a parsed
  // HID Report Descriptor.
  if (iface.interface_subclass == 1) {
    UsbControlTransferRequest proto_req;
    proto_req.device_handle = iface.device_handle;
    proto_req.request_type = 0x21;
    proto_req.request = kHidReqSetProtocol;
    proto_req.value = layout.is_boot_protocol_fallback ? 0 : 1;
    proto_req.index = iface.interface_number;
    proto_req.length = 0;
    (void)device_manager.UsbControlTransfer(proto_req);
  }

  OpenUsbEndpointRequest open_req;
  open_req.device_handle = iface.device_handle;
  open_req.interface_number = iface.interface_number;
  open_req.endpoint_dci = iface.interrupt_in_dci;

  auto ring_or = device_manager.OpenUsbInterruptEndpoint(open_req);
  if (!ring_or) return;

  attached_interfaces.insert(key);
  uint32 handle = iface.device_handle;
  auto on_disconnect = [handle]() {
    Defer([handle]() { DetachDeviceByHandle(handle); });
  };

  switch (layout.device_type) {
    case HidDeviceType::kKeyboard:
      keyboards.push_back(std::make_shared<UsbKeyboardDevice>(
          handle, layout, *ring_or, iface.max_packet_size, on_disconnect));
      break;
    case HidDeviceType::kMouse:
      mice.push_back(std::make_shared<UsbMouseDevice>(
          handle, layout, *ring_or, iface.max_packet_size, on_disconnect));
      SyncTabletAndMousePairing();
      break;
    case HidDeviceType::kTablet:
      tablets.push_back(std::make_shared<UsbTabletDevice>(
          handle, layout, *ring_or, iface.max_packet_size, on_disconnect));
      SyncTabletAndMousePairing();
      break;
    default:
      break;
  }
}

class HidUsbDeviceListener : public UsbDeviceListener::Server {
 public:
  virtual Status UsbInterfaceAttached(const UsbInterfaceInfo& info) override {
    Defer([info]() { AttachHidInterface(info); });
    return Status::OK;
  }

  virtual Status UsbInterfaceDetached(const UsbDeviceId& id) override {
    uint32 handle = id.device_handle;
    Defer([handle]() { DetachDeviceByHandle(handle); });
    return Status::OK;
  }
};

std::unique_ptr<HidUsbDeviceListener> usb_listener;

}  // namespace

int main() {
  if (IsDuplicateInstanceOfProcess()) return 0;

  usb_listener = std::make_unique<HidUsbDeviceListener>();
  auto device_manager = GetService<DeviceManager>();
  device_manager.RegisterUsbDeviceListener(*usb_listener, nullptr);

  UsbInterfaceFilter filter;
  filter.interface_class = kUsbClassHid;
  auto interfaces_or = device_manager.QueryUsbInterfaces(filter);
  if (interfaces_or) {
    for (const auto& iface : interfaces_or->interfaces)
      AttachHidInterface(iface);
  }

  HandOverControl();
  return 0;
}
