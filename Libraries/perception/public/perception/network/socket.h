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
namespace network {

class IpAddress : public serialization::Serializable {
 public:
  uint8 address[4];  // IPv4 address representation

  virtual void Serialize(serialization::Serializer& serializer) override {
    for (int i = 0; i < 4; i++) {
      serializer.Integer("ip_" + std::to_string(i), address[i]);
    }
  }
};

class ConnectRequest : public serialization::Serializable {
 public:
  IpAddress address;
  uint16 port;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Serializable("address", address);
    serializer.Integer("port", port);
  }
};

class BindRequest : public serialization::Serializable {
 public:
  uint16 port;

  virtual void Serialize(serialization::Serializer& serializer) override {
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
  uint64 max_bytes;
  bool non_blocking;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Integer("max_bytes", max_bytes);
    serializer.Integer("non_blocking", non_blocking);
  }
};

class ReceiveResponse : public serialization::Serializable {
 public:
  std::string data;
  bool closed;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.String("data", data);
    serializer.Integer("closed", closed);
  }
};

// Response containing process and message ID to construct the new
// Socket::Client
class AcceptResponse : public serialization::Serializable {
 public:
  ProcessId process_id;
  MessageId message_id;

  virtual void Serialize(serialization::Serializer& serializer) override {
    serializer.Integer("process_id", process_id);
    serializer.Integer("message_id", message_id);
  }
};

#define SOCKET_METHOD_LIST(X)                    \
  X(1, Connect, void, ConnectRequest)            \
  X(2, Bind, void, BindRequest)                  \
  X(3, Listen, void, void)                       \
  X(4, Accept, AcceptResponse, void)             \
  X(5, Send, void, SendRequest)                  \
  X(6, Receive, ReceiveResponse, ReceiveRequest) \
  X(7, Close, void, void)

DEFINE_PERCEPTION_SERVICE(Socket, "perception.network.Socket",
                          SOCKET_METHOD_LIST)
#undef SOCKET_METHOD_LIST

}  // namespace network
}  // namespace perception
