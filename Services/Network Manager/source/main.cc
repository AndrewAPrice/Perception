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

#include <chrono>
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
#include "perception/time.h"
#include "socket.h"

// Cross-module periodic TCP timer entry point implemented in socket.cc.
void TickTcpSockets();

namespace {

using ::perception::AfterDuration;
using ::perception::devices::NetworkDevice;

// Periodic tick interval for network interface and TCP socket state machines.
constexpr auto kNetworkTickInterval = std::chrono::milliseconds(250);

void SchedulePeriodicNetworkTick() {
  AfterDuration(kNetworkTickInterval, []() {
    TickNetworkInterfaces();
    TickTcpSockets();
    SchedulePeriodicNetworkTick();
  });
}

}  // namespace

int main() {
  ::perception::SetThreadPriority(
      ::perception::ThreadPriority::RealtimeService);
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

        const size_t next_idx = GetNetworkInterfaceCount();
        CreateAndAddNetworkListener(next_idx, device);
        (void)AddNetworkInterface(std::move(iface));
      });

  SchedulePeriodicNetworkTick();

  NetworkService network_service;
  perception::HandOverControl();
  return 0;
}
