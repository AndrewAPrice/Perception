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

#include <string>
#include <vector>

#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/service_macros.h"

namespace perception {
namespace devices {

// Serializable container for network packets.
class Packet : public serialization::Serializable {
 public:
  std::string data;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("data", data);
  }
};

// Serializable container for MAC addresses.
class MacAddress : public serialization::Serializable {
 public:
  uint8 mac[6];

  virtual void Serialize(serialization::Serializer& serializer) override {
    for (int i = 0; i < 6; i++) {
      serializer.Integer("mac_" + std::to_string(i), mac[i]);
    }
  }
};

// Listener service implemented by the Network Manager to receive packets from
// drivers.
#define NETWORK_LISTENER_METHOD_LIST(X) X(1, PacketReceived, void, Packet)

DEFINE_PERCEPTION_SERVICE(NetworkListener, "perception.devices.NetworkListener",
                          NETWORK_LISTENER_METHOD_LIST)
#undef NETWORK_LISTENER_METHOD_LIST

// Service implemented by NIC drivers.
#define NETWORK_DEVICE_METHOD_LIST(X)   \
  X(1, GetMacAddress, MacAddress, void) \
  X(2, SendPacket, void, Packet)        \
  X(3, SetPacketListener, void, NetworkListener::Client)

DEFINE_PERCEPTION_SERVICE(NetworkDevice, "perception.devices.NetworkDevice",
                          NETWORK_DEVICE_METHOD_LIST)
#undef NETWORK_DEVICE_METHOD_LIST

}  // namespace devices
}  // namespace perception
