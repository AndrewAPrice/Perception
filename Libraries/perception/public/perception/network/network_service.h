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

#include "perception/devices/network_device.h"
#include "perception/network/socket.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/service_macros.h"

namespace perception {
namespace network {

enum class SocketProtocol : uint8 { TCP = 0, UDP = 1 };

class CreateSocketRequest : public serialization::Serializable {
 public:
  SocketProtocol protocol = SocketProtocol::TCP;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Integer("protocol", protocol);
  }
};

class CreateSocketResponse : public serialization::Serializable {
 public:
  Socket::Client socket;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("socket", socket);
  }
};

class ResolveHostRequest : public serialization::Serializable {
 public:
  std::string host;
  IpAddressFamily family = IpAddressFamily::Unspecified;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("host", host);
    serializer.Integer("family", family);
  }
};

class ResolveHostResponse : public serialization::Serializable {
 public:
  std::vector<IpAddress> addresses;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.ArrayOfSerializables("addresses", addresses);
  }
};

class ConnectToHostRequest : public serialization::Serializable {
 public:
  std::string host;
  uint16 port = 0;
  SocketProtocol protocol = SocketProtocol::TCP;
  IpAddressFamily family = IpAddressFamily::Unspecified;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("host", host);
    serializer.Integer("port", port);
    serializer.Integer("protocol", protocol);
    serializer.Integer("family", family);
  }
};

class ConnectToHostResponse : public serialization::Serializable {
 public:
  Socket::Client socket;
  IpAddress remote_address;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("socket", socket);
    serializer.Serializable("remote_address", remote_address);
  }
};

class InterfaceAddressInfo : public serialization::Serializable {
 public:
  IpAddress address;
  uint8 prefix_length = 0;
  uint8 state = 0;
  uint8 origin = 0;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("address", address);
    serializer.Integer("prefix_length", prefix_length);
    serializer.Integer("state", state);
    serializer.Integer("origin", origin);
  }
};

class InterfaceInfo : public serialization::Serializable {
 public:
  devices::MacAddress mac;
  std::vector<InterfaceAddressInfo> addresses;
  std::vector<IpAddress> default_routers;
  std::vector<IpAddress> dns_servers;
  uint16 mtu = 1500;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("mac", mac);
    serializer.ArrayOfSerializables("addresses", addresses);
    serializer.ArrayOfSerializables("default_routers", default_routers);
    serializer.ArrayOfSerializables("dns_servers", dns_servers);
    serializer.Integer("mtu", mtu);
  }
};

class GetInterfacesResponse : public serialization::Serializable {
 public:
  std::vector<InterfaceInfo> interfaces;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.ArrayOfSerializables("interfaces", interfaces);
  }
};

#define NETWORK_SERVICE_METHOD_LIST(X)                          \
  X(1, CreateSocket, CreateSocketResponse, CreateSocketRequest) \
  X(2, ResolveHost, ResolveHostResponse, ResolveHostRequest)    \
  X(3, ConnectToHost, ConnectToHostResponse, ConnectToHostRequest) \
  X(4, GetInterfaces, GetInterfacesResponse, void)

DEFINE_PERCEPTION_SERVICE(NetworkService, "perception.network.NetworkService",
                          NETWORK_SERVICE_METHOD_LIST)
#undef NETWORK_SERVICE_METHOD_LIST

}  // namespace network
}  // namespace perception
