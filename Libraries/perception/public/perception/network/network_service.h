// Copyright 2026 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance on the License.
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
#include "perception/network/socket.h"

namespace perception {
namespace network {

enum class SocketProtocol : uint8 { TCP = 0, UDP = 1 };

class CreateSocketRequest : public serialization::Serializable {
 public:
  SocketProtocol protocol;

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

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("host", host);
  }
};

class ResolveHostResponse : public serialization::Serializable {
 public:
  std::vector<IpAddress> addresses;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.ArrayOfSerializables("addresses", addresses);
  }
};

#define NETWORK_SERVICE_METHOD_LIST(X)                          \
  X(1, CreateSocket, CreateSocketResponse, CreateSocketRequest) \
  X(2, ResolveHost, ResolveHostResponse, ResolveHostRequest)

DEFINE_PERCEPTION_SERVICE(NetworkService, "perception.network.NetworkService",
                           NETWORK_SERVICE_METHOD_LIST)
#undef NETWORK_SERVICE_METHOD_LIST

}  // namespace network
}  // namespace perception
