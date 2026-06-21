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

#include <memory>
#include <string>
#include <vector>

#include "perception/fibers.h"
#include "perception/network/socket.h"

// Supported layer 4 socket transport protocols.
enum class SocketType { TCP, UDP };

// Real, robust socket implementation subclassing the auto-generated Socket
// Server.
class SocketImpl : public ::perception::network::Socket::Server,
                   public std::enable_shared_from_this<SocketImpl> {
 public:
  // TCP state machine states representation.
  enum TcpState {
    ClosedState,
    SynSentState,
    SynReceivedState,
    EstablishedState,
    FinWait1State,
    FinWait2State,
    CloseWaitState,
    LastAckState,
    TimeWaitState,
    ListenState
  };

  // Initializes a socket instance of the given protocol type.
  SocketImpl(SocketType type)
      : ::perception::network::Socket::Server(),
        type_(type),
        state_(ClosedState),
        local_port_(0),
        remote_port_(0),
        remote_ip_(0),
        seq_(0),
        ack_(0),
        blocked_fiber_(nullptr) {}

  // Initiates an outbound TCP handshake connection or associates a UDP socket.
  virtual ::perception::Status Connect(
      const ::perception::network::ConnectRequest& request) override;

  // Binds the socket to a local port.
  virtual ::perception::Status Bind(
      const ::perception::network::BindRequest& request) override;

  // Places a TCP socket into passive listening mode.
  virtual ::perception::Status Listen() override;

  // Blocks the executing fiber until a new inbound TCP connection is accepted.
  virtual StatusOr<::perception::network::AcceptResponse> Accept() override;

  // Transmits data payload over TCP or UDP.
  virtual ::perception::Status Send(
      const ::perception::network::SendRequest& request) override;

  // Retrieves buffered received payload bytes, blocking the fiber if empty.
  virtual StatusOr<::perception::network::ReceiveResponse> Receive(
      const ::perception::network::ReceiveRequest& request) override;

  // Initiates connection teardown (e.g., sending TCP FIN segment).
  virtual ::perception::Status Close() override;

  SocketType GetType() const;
  TcpState GetState() const;
  void SetState(TcpState state);

  uint16 GetLocalPort() const;
  void SetLocalPort(uint16 port);

  uint16 GetRemotePort() const;
  void SetRemotePort(uint16 port);

  uint32 GetRemoteIp() const;
  void SetRemoteIp(uint32 ip);

  uint32 GetSeq() const;
  void SetSeq(uint32 seq);

  uint32 GetAck() const;
  void SetAck(uint32 ack);

  void AppendRxBuffer(const std::string& data);

  void QueueUdpPacket(uint32 src_ip, uint16 src_port, const std::string& data);

  void QueueAcceptedSocket(std::shared_ptr<SocketImpl> socket);

  ::perception::Fiber* GetBlockedFiber() const;
  void SetBlockedFiber(::perception::Fiber* fiber);

 private:
  // Socket layer 4 protocol type (TCP or UDP).
  SocketType type_;
  // Current connection state (for TCP sockets).
  TcpState state_;
  // Local port bound in host-endianness.
  uint16 local_port_;
  // Remote target host port in host-endianness.
  uint16 remote_port_;
  // Remote target host IP address in host-endianness.
  uint32 remote_ip_;
  // Local sequence counter tracking transmitted bytes.
  uint32 seq_;
  // Acknowledgment counter tracking received bytes.
  uint32 ack_;

  // Receive stream buffer for TCP sockets.
  std::string rx_buffer_;
  // Represents a structured received UDP packet.
  struct UdpPacket {
    uint32 src_ip;
    uint16 src_port;
    std::string data;
  };
  // Receive buffer queue for UDP sockets.
  std::vector<UdpPacket> udp_rx_queue_;
  // Backlog queue storing newly accepted client socket servers.
  std::vector<std::shared_ptr<SocketImpl>> accept_queue_;

  // Pointer to the fiber currently blocked on a socket operation
  // (Accept/Receive/Connect).
  ::perception::Fiber* blocked_fiber_;
};

// Adds a new active socket to the global registry list.
void AddActiveSocket(std::shared_ptr<SocketImpl> socket);

// Retrieves the global registry list containing all active sockets.
const std::vector<std::shared_ptr<SocketImpl>>& GetActiveSockets();

// Composes and transmits a raw TCP segment with optional flags and payload.
void SendTcpPacket(size_t iface_idx, std::shared_ptr<SocketImpl> sock,
                   uint8 flags_val, const std::string& payload = "");

// Composes and transmits a raw UDP datagram containing the specified payload.
void SendUdpPacket(size_t iface_idx, uint32 dest_ip, uint16 src_port,
                   uint16 dest_port, const std::string& payload);

// Dispatches a newly received raw UDP payload to its matching listening UDP
// socket.
void DispatchUdpPacket(uint32 src_ip, uint16 src_port, uint16 dest_port,
                       const uint8* payload, size_t len);
