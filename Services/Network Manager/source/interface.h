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

#include <types.h>

#include <utility>
#include <vector>

#include "perception/devices/network_device.h"
#include "perception/fibers.h"

// Represents an active network interface card (NIC).
struct NetworkInterface {
  // Client handle connected to the network driver instance.
  ::perception::devices::NetworkDevice::Client device;
  // Local MAC hardware address of this card.
  uint8 mac[6];
  // Statically configured local IPv4 address in host byte order.
  uint32 ip;
  // Local router gateway IPv4 address in host byte order.
  uint32 gateway_ip;
  // Resolved hardware MAC address of the default gateway.
  uint8 gateway_mac[6];
  // Flag indicating whether the gateway's MAC address has been resolved via
  // ARP.
  bool gateway_mac_resolved;
};

// Retrieves all registered active network interfaces.
const std::vector<NetworkInterface>& GetNetworkInterfaces();

// Retrieves a registered network interface by index.
NetworkInterface& GetNetworkInterface(size_t index);

// Retrieves the total number of registered network interfaces.
size_t GetNetworkInterfaceCount();

// Adds a new network interface card and returns its assigned index.
size_t AddNetworkInterface(NetworkInterface interface);

// Wakes up any fibers currently suspended waiting for ARP resolution on a given
// interface.
void WakeFibersWaitingForArp(size_t iface_idx);

// Suspends the currently executing fiber until ARP resolution is successful on
// the given interface.
void WaitForArp(size_t iface_idx);

// Composes and broadcasts an ARP request packet targeting the specified IP
// address.
void SendArpRequest(size_t iface_idx, uint32 target_ip);

// Composes and transmits an ARP reply packet back to a requesting target host.
void SendArpReply(size_t iface_idx, const uint8* target_mac, uint32 target_ip);
