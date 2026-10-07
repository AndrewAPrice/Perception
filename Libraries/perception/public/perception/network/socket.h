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

#include "perception/network/ip_address.h"
#include "perception/serialization/serializable.h"
#include "perception/serialization/serializer.h"
#include "perception/service_macros.h"

namespace perception {
namespace network {

class ConnectRequest : public serialization::Serializable {
 public:
  IpAddress address;
  uint16 port = 0;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("address", address);
    serializer.Integer("port", port);
  }
};

class BindRequest : public serialization::Serializable {
 public:
  IpAddress address;
  uint16 port = 0;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("address", address);
    serializer.Integer("port", port);
  }
};

class SendRequest : public serialization::Serializable {
 public:
  std::string data;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("data", data);
  }
};

class ReceiveRequest : public serialization::Serializable {
 public:
  uint64 max_bytes = 0;
  bool non_blocking = false;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Integer("max_bytes", max_bytes);
    serializer.Integer("non_blocking", non_blocking);
  }
};

class ReceiveResponse : public serialization::Serializable {
 public:
  std::string data;
  bool closed = false;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("data", data);
    serializer.Integer("closed", closed);
  }
};

// Response containing process and message ID to construct the new
// Socket::Client, along with the peer's remote address and port.
class AcceptResponse : public serialization::Serializable {
 public:
  ProcessId process_id = 0;
  MessageId message_id = 0;
  IpAddress remote_address;
  uint16 remote_port = 0;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Integer("process_id", process_id);
    serializer.Integer("message_id", message_id);
    serializer.Serializable("remote_address", remote_address);
    serializer.Integer("remote_port", remote_port);
  }
};

// Local and remote endpoints of a bound or connected socket.
class SocketEndpoints : public serialization::Serializable {
 public:
  IpAddress local_address;
  uint16 local_port = 0;
  IpAddress remote_address;
  uint16 remote_port = 0;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("local_address", local_address);
    serializer.Integer("local_port", local_port);
    serializer.Serializable("remote_address", remote_address);
    serializer.Integer("remote_port", remote_port);
  }
};

#define SOCKET_METHOD_LIST(X)                    \
  X(1, Connect, void, ConnectRequest)            \
  X(2, Bind, void, BindRequest)                  \
  X(3, Listen, void, void)                       \
  X(4, Accept, AcceptResponse, void)             \
  X(5, Send, void, SendRequest)                  \
  X(6, Receive, ReceiveResponse, ReceiveRequest) \
  X(7, Close, void, void)                        \
  X(8, GetEndpoints, SocketEndpoints, void)

DEFINE_PERCEPTION_SERVICE(Socket, "perception.network.Socket",
                          SOCKET_METHOD_LIST)
#undef SOCKET_METHOD_LIST

}  // namespace network
}  // namespace perception
