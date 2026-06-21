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

#include "socket.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#include "dns.h"
#include "interface.h"
#include "perception/time.h"
#include "protocols.h"

using ::perception::Status;

namespace {

std::vector<std::shared_ptr<SocketImpl>> active_sockets;

}  // namespace

void AddActiveSocket(std::shared_ptr<SocketImpl> socket) {
  active_sockets.push_back(socket);
}

const std::vector<std::shared_ptr<SocketImpl>>& GetActiveSockets() {
  return active_sockets;
}

void SendTcpPacket(size_t iface_idx, std::shared_ptr<SocketImpl> sock,
                   uint8 flags_val, const std::string& payload) {
  auto& iface = GetNetworkInterface(iface_idx);
  if (!iface.gateway_mac_resolved) {
    SendArpRequest(iface_idx, iface.gateway_ip);
    while (!iface.gateway_mac_resolved) {
      WaitForArp(iface_idx);
    }
  }

  size_t ip_payload_len = sizeof(TcpHeader) + payload.length();
  std::string packet_data(
      sizeof(EthernetHeader) + sizeof(IpHeader) + ip_payload_len, '\0');

  EthernetHeader* eth = (EthernetHeader*)packet_data.data();
  for (int i = 0; i < 6; i++) {
    eth->dest_mac[i] = iface.gateway_mac[i];
    eth->src_mac[i] = iface.mac[i];
  }
  eth->type = Swap16BitEndian(0x0800);

  IpHeader* ip = (IpHeader*)(packet_data.data() + sizeof(EthernetHeader));
  ip->version_ihl = 0x45;
  ip->tos = 0;
  ip->len = Swap16BitEndian(sizeof(IpHeader) + ip_payload_len);
  ip->id = Swap16BitEndian(1234);
  ip->flags_offset = Swap16BitEndian(0x4000);
  ip->ttl = 64;
  ip->protocol = 6;
  ip->src_ip = iface.ip;
  ip->dest_ip = sock->GetRemoteIp();
  ip->checksum = 0;
  ip->checksum = CalculateChecksum((const uint16*)ip, sizeof(IpHeader));

  TcpHeader* tcp = (TcpHeader*)(packet_data.data() + sizeof(EthernetHeader) +
                                sizeof(IpHeader));
  tcp->src_port = Swap16BitEndian(sock->GetLocalPort());
  tcp->dest_port = Swap16BitEndian(sock->GetRemotePort());
  tcp->seq = Swap32BitEndian(sock->GetSeq());
  tcp->ack = Swap32BitEndian(sock->GetAck());
  tcp->flags = Swap16BitEndian(((uint16)5 << 12) | flags_val);
  tcp->window = Swap16BitEndian(65535);
  tcp->checksum = 0;
  tcp->urgent = 0;

  if (!payload.empty()) {
    std::memcpy(packet_data.data() + sizeof(EthernetHeader) + sizeof(IpHeader) +
                    sizeof(TcpHeader),
                payload.data(), payload.length());
  }

  tcp->checksum = CalculateTcpChecksum(ip->src_ip, ip->dest_ip,
                                       (const uint8*)tcp, ip_payload_len);

  ::perception::devices::Packet pkt;
  pkt.data = packet_data;
  iface.device.SendPacket(pkt);
}

void SendUdpPacket(size_t iface_idx, uint32 dest_ip, uint16 src_port,
                   uint16 dest_port, const std::string& payload) {
  auto& iface = GetNetworkInterface(iface_idx);

  bool is_dns_server = (dest_ip == iface.gateway_ip + Swap32BitEndian(1));
  if (is_dns_server) {
    if (!IsDnsMacResolved()) {
      SendArpRequest(iface_idx, dest_ip);
      while (!IsDnsMacResolved()) {
        WaitForArp(iface_idx);
      }
    }
  } else {
    if (!iface.gateway_mac_resolved) {
      SendArpRequest(iface_idx, iface.gateway_ip);
      while (!iface.gateway_mac_resolved) {
        WaitForArp(iface_idx);
      }
    }
  }

  size_t ip_payload_len = sizeof(UdpHeader) + payload.length();
  std::string packet_data(
      sizeof(EthernetHeader) + sizeof(IpHeader) + ip_payload_len, '\0');

  EthernetHeader* eth = (EthernetHeader*)packet_data.data();
  const uint8* target_mac = is_dns_server ? GetDnsMac().mac : iface.gateway_mac;
  for (int i = 0; i < 6; i++) {
    eth->dest_mac[i] = target_mac[i];
    eth->src_mac[i] = iface.mac[i];
  }
  eth->type = Swap16BitEndian(0x0800);

  IpHeader* ip = (IpHeader*)(packet_data.data() + sizeof(EthernetHeader));
  ip->version_ihl = 0x45;
  ip->tos = 0;
  ip->len = Swap16BitEndian(sizeof(IpHeader) + ip_payload_len);
  ip->id = Swap16BitEndian(5678);
  ip->flags_offset = 0;
  ip->ttl = 64;
  ip->protocol = 17;
  ip->src_ip = iface.ip;
  ip->dest_ip = dest_ip;
  ip->checksum = 0;
  ip->checksum = CalculateChecksum((const uint16*)ip, sizeof(IpHeader));

  UdpHeader* udp = (UdpHeader*)(packet_data.data() + sizeof(EthernetHeader) +
                                sizeof(IpHeader));
  udp->src_port = Swap16BitEndian(src_port);
  udp->dest_port = Swap16BitEndian(dest_port);
  udp->len = Swap16BitEndian(ip_payload_len);
  if (!payload.empty()) {
    std::memcpy(packet_data.data() + sizeof(EthernetHeader) + sizeof(IpHeader) +
                    sizeof(UdpHeader),
                payload.data(), payload.length());
  }

  udp->checksum = 0;
  udp->checksum = CalculateUdpChecksum(ip->src_ip, ip->dest_ip,
                                       (const uint8*)udp, ip_payload_len);
  if (udp->checksum == 0) udp->checksum = 0xFFFF;

  ::perception::devices::Packet pkt;
  pkt.data = packet_data;
  iface.device.SendPacket(pkt);
}

void DispatchUdpPacket(uint32 src_ip, uint16 src_port, uint16 dest_port,
                       const uint8* payload, size_t len) {
  for (auto& sock : active_sockets) {
    if (sock->GetType() == SocketType::UDP &&
        sock->GetLocalPort() == dest_port) {
      sock->QueueUdpPacket(src_ip, src_port,
                           std::string((const char*)payload, len));
      if (sock->GetBlockedFiber()) {
        sock->GetBlockedFiber()->WakeUp();
        sock->SetBlockedFiber(nullptr);
      }
      return;
    }
  }
}

Status SocketImpl::Connect(
    const ::perception::network::ConnectRequest& request) {
  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  remote_ip_ = IpToUint32(request.address);
  remote_port_ = request.port;

  if (type_ == SocketType::UDP) {
    local_port_ = 49152 + (active_sockets.size() % 16384);
    state_ = EstablishedState;
    return Status::OK;
  }

  local_port_ = 49152 + (active_sockets.size() % 16384);
  state_ = SynSentState;
  seq_ = 1000;
  ack_ = 0;

  SendTcpPacket(0, shared_from_this(), 0x02);  // SYN
  seq_++;

  blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();

  auto current_fiber = blocked_fiber_;
  auto connected_ptr = std::make_shared<bool>(false);

  auto timeout_fiber =
      ::perception::Fiber::Create([current_fiber, connected_ptr]() {
        ::perception::SleepForDuration(std::chrono::seconds(15));
        if (!*connected_ptr) {
          current_fiber->WakeUp();
        }
      });
  timeout_fiber->WakeUp();

  ::perception::Sleep();
  *connected_ptr = true;

  blocked_fiber_ = nullptr;

  if (state_ == EstablishedState) {
    return Status::OK;
  } else {
    return Status::INTERNAL_ERROR;
  }
}

Status SocketImpl::Bind(const ::perception::network::BindRequest& request) {
  local_port_ = request.port;
  return Status::OK;
}

Status SocketImpl::Listen() {
  state_ = ListenState;
  return Status::OK;
}

StatusOr<::perception::network::AcceptResponse> SocketImpl::Accept() {
  while (accept_queue_.empty()) {
    blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
    ::perception::Sleep();
  }

  auto client_sock = accept_queue_.front();
  accept_queue_.erase(accept_queue_.begin());

  ::perception::network::AcceptResponse response;
  response.process_id = client_sock->ServerProcessId();
  response.message_id = client_sock->ServiceId();
  return response;
}

Status SocketImpl::Send(const ::perception::network::SendRequest& request) {
  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  if (type_ == SocketType::UDP) {
    SendUdpPacket(0, remote_ip_, local_port_, remote_port_, request.data);
    return Status::OK;
  }

  if (state_ != EstablishedState && state_ != CloseWaitState)
    return Status::INTERNAL_ERROR;

  SendTcpPacket(0, shared_from_this(), 0x18, request.data);  // PUSH | ACK
  seq_ += request.data.length();

  return Status::OK;
}

StatusOr<::perception::network::ReceiveResponse> SocketImpl::Receive(
    const ::perception::network::ReceiveRequest& request) {
  if (type_ == SocketType::UDP) {
    while (udp_rx_queue_.empty()) {
      blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
      ::perception::Sleep();
    }

    auto pkt = udp_rx_queue_.front();
    udp_rx_queue_.erase(udp_rx_queue_.begin());

    ::perception::network::ReceiveResponse response;
    response.data = pkt.data;
    return response;
  }

  if (rx_buffer_.empty() && state_ == EstablishedState) {
    if (request.non_blocking) {
      ::perception::network::ReceiveResponse response;
      response.closed = false;
      return response;
    }
    blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
    ::perception::Sleep();
  }

  ::perception::network::ReceiveResponse response;
  response.closed = (state_ != EstablishedState && rx_buffer_.empty());
  size_t bytes_to_copy =
      std::min((size_t)request.max_bytes, rx_buffer_.length());
  if (bytes_to_copy > 0) {
    response.data = rx_buffer_.substr(0, bytes_to_copy);
    rx_buffer_ = rx_buffer_.substr(bytes_to_copy);
  }
  return response;
}

Status SocketImpl::Close() {
  if (type_ == SocketType::TCP) {
    if (state_ == EstablishedState) {
      state_ = FinWait1State;
      SendTcpPacket(0, shared_from_this(), 0x11);  // FIN | ACK
      seq_++;
    } else if (state_ == CloseWaitState) {
      state_ = LastAckState;
      SendTcpPacket(0, shared_from_this(), 0x11);  // FIN | ACK
      seq_++;
    } else {
      state_ = ClosedState;
    }
  } else {
    state_ = ClosedState;
  }
  return Status::OK;
}

SocketType SocketImpl::GetType() const { return type_; }

SocketImpl::TcpState SocketImpl::GetState() const { return state_; }

void SocketImpl::SetState(TcpState state) { state_ = state; }

uint16 SocketImpl::GetLocalPort() const { return local_port_; }

void SocketImpl::SetLocalPort(uint16 port) { local_port_ = port; }

uint16 SocketImpl::GetRemotePort() const { return remote_port_; }

void SocketImpl::SetRemotePort(uint16 port) { remote_port_ = port; }

uint32 SocketImpl::GetRemoteIp() const { return remote_ip_; }

void SocketImpl::SetRemoteIp(uint32 ip) { remote_ip_ = ip; }

uint32 SocketImpl::GetSeq() const { return seq_; }

void SocketImpl::SetSeq(uint32 seq) { seq_ = seq; }

uint32 SocketImpl::GetAck() const { return ack_; }

void SocketImpl::SetAck(uint32 ack) { ack_ = ack; }

void SocketImpl::AppendRxBuffer(const std::string& data) { rx_buffer_ += data; }

void SocketImpl::QueueUdpPacket(uint32 src_ip, uint16 src_port,
                                const std::string& data) {
  udp_rx_queue_.push_back({src_ip, src_port, data});
}

void SocketImpl::QueueAcceptedSocket(std::shared_ptr<SocketImpl> socket) {
  accept_queue_.push_back(socket);
}

::perception::Fiber* SocketImpl::GetBlockedFiber() const {
  return blocked_fiber_;
}

void SocketImpl::SetBlockedFiber(::perception::Fiber* fiber) {
  blocked_fiber_ = fiber;
}
