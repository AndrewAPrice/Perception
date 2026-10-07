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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ip_endpoint.h"
#include "perception/fibers.h"
#include "perception/network/socket.h"
#include "status.h"
#include "tcp_isn.h"
#include "tcp_options.h"
#include "tcp_sender.h"
#include "tcp_sequence.h"
#include "tcp_time_wait.h"
#include "tcp_validation.h"

// Supported layer 4 socket transport protocols.
enum class SocketType { TCP, UDP };

// Socket server implementation backed by the Network Manager stack.
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
  explicit SocketImpl(SocketType type)
      : ::perception::network::Socket::Server(),
        type_(type),
        state_(ClosedState),
        seq_(0),
        ack_(0),
        blocked_fiber_(nullptr) {}

  // Initiates an outbound TCP handshake connection or associates a UDP socket.
  virtual Status Connect(
      const ::perception::network::ConnectRequest& request) override;

  // Binds the socket to a local address and port.
  virtual Status Bind(
      const ::perception::network::BindRequest& request) override;

  // Places a TCP socket into passive listening mode.
  virtual Status Listen() override;

  // Blocks the executing fiber until a new inbound TCP connection is accepted.
  virtual StatusOr<::perception::network::AcceptResponse> Accept() override;

  // Transmits data payload over TCP or UDP.
  virtual Status Send(
      const ::perception::network::SendRequest& request) override;

  // Retrieves buffered received payload bytes, blocking the fiber if empty.
  virtual StatusOr<::perception::network::ReceiveResponse> Receive(
      const ::perception::network::ReceiveRequest& request) override;

  // Initiates connection teardown (e.g., sending TCP FIN segment).
  virtual Status Close() override;

  // Returns the local and remote endpoints of this socket.
  virtual StatusOr<::perception::network::SocketEndpoints> GetEndpoints()
      override;

  SocketType GetType() const;
  TcpState GetState() const;
  void SetState(TcpState state);

  const IpEndpoint& GetLocalEndpoint() const;
  void SetLocalEndpoint(const IpEndpoint& endpoint);

  const IpEndpoint& GetRemoteEndpoint() const;
  void SetRemoteEndpoint(const IpEndpoint& endpoint);

  uint16 GetLocalPort() const;
  void SetLocalPort(uint16 port);

  uint16 GetRemotePort() const;
  void SetRemotePort(uint16 port);

  const ::perception::network::IpAddress& GetRemoteIp() const;
  void SetRemoteIp(const ::perception::network::IpAddress& ip);

  uint32 GetSeq() const;
  void SetSeq(uint32 seq);

  uint32 GetAck() const;
  void SetAck(uint32 ack);

  size_t GetInterfaceIndex() const;
  void SetInterfaceIndex(size_t iface_idx);

  uint16 GetReceiveWindow() const;

  void AppendRxBuffer(const std::string& data);

  void QueueUdpPacket(const ::perception::network::IpAddress& src_ip,
                      uint16 src_port, const std::string& data);

  void QueueAcceptedSocket(std::shared_ptr<SocketImpl> socket);

  ::perception::Fiber* GetBlockedFiber() const;
  void SetBlockedFiber(::perception::Fiber* fiber);
  void WakeBlockedFiber();

  void InitTcpSender(uint32 iss, std::optional<uint16> peer_mss, TcpTime now,
                     bool send_syn);
  TcpSender* GetTcpSender();

  void EnterTimeWait(TcpTime now);
  TcpTimeWait* GetTimeWait();

  Status GetLastError() const;
  void SetLastError(Status status);

  void UpdateEffectiveMssFromPmtu(uint16 new_pmtu);

 private:
  // Socket layer 4 protocol type (TCP or UDP).
  SocketType type_;
  // Current connection state (for TCP sockets).
  TcpState state_;
  // Local transport endpoint (address + port).
  IpEndpoint local_;
  // Remote transport endpoint (address + port).
  IpEndpoint remote_;
  // Outgoing network interface index.
  size_t iface_idx_ = 0;
  // Local sequence counter tracking transmitted bytes.
  uint32 seq_;
  // Acknowledgment counter tracking received bytes.
  uint32 ack_;
  // Peer's advertised MSS option, if received during the handshake.
  std::optional<uint16> peer_mss_;
  // Reliability and congestion control engine for the send half of TCP.
  std::unique_ptr<TcpSender> sender_;
  // 2*MSL TIME_WAIT tracker when in TimeWaitState.
  std::optional<TcpTimeWait> time_wait_;
  // Asynchronous error reported by ICMP or timeout.
  Status last_error_ = Status::OK;

  // Receive stream buffer for TCP sockets.
  std::string rx_buffer_;
  // Represents a structured received UDP packet.
  struct UdpPacket {
    ::perception::network::IpAddress src_ip;
    uint16 src_port;
    std::string data;
  };
  // Receive buffer queue for UDP sockets.
  std::vector<UdpPacket> udp_rx_queue_;
  // Backlog queue storing newly accepted client socket servers.
  std::vector<std::shared_ptr<SocketImpl>> accept_queue_;

  // Pointer to the fiber currently blocked on a socket operation.
  ::perception::Fiber* blocked_fiber_;
};

// Adds a new active socket to the global registry list.
void AddActiveSocket(std::shared_ptr<SocketImpl> socket);

// Retrieves the global registry list containing all active sockets.
const std::vector<std::shared_ptr<SocketImpl>>& GetActiveSockets();

// Composes and transmits a raw TCP segment with optional flags, payload, and
// header options.
void SendTcpPacket(size_t iface_idx, std::shared_ptr<SocketImpl> sock,
                   uint8 flags_val, const std::string& payload = "",
                   const std::string& options = "");

// Composes and transmits a raw UDP datagram with explicit source IP.
Status SendUdpPacket(size_t iface_idx,
                     const ::perception::network::IpAddress& src_ip,
                     uint16 src_port,
                     const ::perception::network::IpAddress& dest_ip,
                     uint16 dest_port, const std::string& payload);

// Composes and transmits a raw UDP datagram, selecting the source IP on
// `iface_idx`.
Status SendUdpPacket(size_t iface_idx, uint16 src_port,
                     const ::perception::network::IpAddress& dest_ip,
                     uint16 dest_port, const std::string& payload);

// Overload matching legacy parameter order (`dest_ip` before `src_port`).
Status SendUdpPacket(size_t iface_idx,
                     const ::perception::network::IpAddress& dest_ip,
                     uint16 src_port, uint16 dest_port,
                     const std::string& payload);

// Dispatches a received UDP payload to its matching bound or connected socket.
void DispatchUdpPacket(const ::perception::network::IpAddress& src_ip,
                       uint16 src_port,
                       const ::perception::network::IpAddress& dst_ip,
                       uint16 dest_port, const uint8* payload, size_t len);

// Overload dispatching a received UDP payload when only `dest_port` is given.
void DispatchUdpPacket(const ::perception::network::IpAddress& src_ip,
                       uint16 src_port, uint16 dest_port, const uint8* payload,
                       size_t len);

// Processes an incoming TCP segment over IPv4 or IPv6 on `iface_idx`.
void ProcessTcpSegment(size_t iface_idx,
                       const ::perception::network::IpAddress& src_ip,
                       const ::perception::network::IpAddress& dst_ip,
                       std::string_view tcp_segment);

// Delivers an ICMP/ICMPv6 error or Path MTU update to the matching socket.
void NotifySocketIcmpError(const ::perception::network::IpAddress& src_ip,
                           const ::perception::network::IpAddress& dst_ip,
                           uint8 inner_protocol,
                           std::string_view inner_transport_header,
                           Status error_status, uint16 new_pmtu = 0);

// Advances TCP retransmission, persist, and TIME_WAIT timers across all active
// sockets.
void TickTcpSockets();
