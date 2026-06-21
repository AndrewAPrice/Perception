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

#include <iostream>
#include <memory>
#include <utility>

#include "interface.h"
#include "network_listener.h"
#include "network_service.h"
#include "perception/devices/network_device.h"
#include "perception/scheduler.h"
#include "perception/services.h"
#include "perception/threads.h"

using ::perception::devices::NetworkDevice;

int main() {
  ::perception::SetThreadPriority(
      ::perception::ThreadPriority::RealtimeService);
  // Listen for any Network Devices (NICs)
  ::perception::NotifyOnEachNewServiceInstance<NetworkDevice>(
      [](NetworkDevice::Client device) {
        auto status_or_mac = device.GetMacAddress();
        if (!status_or_mac) {
          std::cout << "Network Manager: Failed to get MAC address."
                    << std::endl;
          return;
        }

        NetworkInterface iface;
        iface.device = device;
        for (int i = 0; i < 6; i++) iface.mac[i] = status_or_mac->mac[i];
        iface.ip = (10) | (0 << 8) | (2 << 16) | (15 << 24);  // 10.0.2.15
        iface.gateway_ip = (10) | (0 << 8) | (2 << 16) | (2 << 24);  // 10.0.2.2
        iface.gateway_mac_resolved = false;

        size_t iface_idx = AddNetworkInterface(std::move(iface));

        // Create listener server for this device.
        CreateAndAddNetworkListener(iface_idx);
      });

  NetworkService network_service;
  perception::HandOverControl();
  return 0;
}
