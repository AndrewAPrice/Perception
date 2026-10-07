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
#include <iostream>
#include <span>

#include "checksum.h"
#include "endian.h"
#include "interface.h"
#include "ip.h"
#include "perception/time.h"
#include "protocols.h"

namespace {

using ::perception::network::AcceptResponse;
using ::perception::network::BindRequest;
using ::perception::network::ConnectRequest;
using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;
using ::perception::network::ReceiveRequest;
using ::perception::network::ReceiveResponse;
using ::perception::network::SendRequest;
using ::perception::network::SocketEndpoints;

// IPv4/IPv6 protocol number for TCP (6).
constexpr uint8 kProtocolTcp = 6;

// IPv4/IPv6 protocol number for UDP (17).
constexpr uint8 kProtocolUdp = 17;

// Default TCP receive window size (65535).
constexpr uint16 kDefaultTcpWindow = 65535;

// Base ephemeral port for outbound sockets (RFC 6335).
constexpr uint16 kEphemeralPortBase = 49152;

// Number of ephemeral ports in the dynamic range [49152, 65535].
constexpr uint16 kEphemeralPortCount = 16384;

// TCP flag bit for FIN.
constexpr uint8 kTcpFlagFin = 0x01;

// TCP flag bit for SYN.
constexpr uint8 kTcpFlagSyn = 0x02;

// TCP flag bit for RST.
constexpr uint8 kTcpFlagRst = 0x04;

// TCP flag bit for PSH.
constexpr uint8 kTcpFlagPsh = 0x08;

// TCP flag bit for ACK.
constexpr uint8 kTcpFlagAck = 0x10;

// Default timeout for blocking TCP Connect calls in seconds.
constexpr int kTcpConnectTimeoutSeconds = 15;

// Fixed seed bytes mixed with boot clock cycles for RFC 6528 ISN generation.
constexpr SipHashKey kDefaultIsnSeed = {0x50, 0x65, 0x72, 0x63, 0x65, 0x70,
                                        0x74, 0x69, 0x6f, 0x6e, 0x54, 0x63,
                                        0x70, 0x49, 0x73, 0x6e};

std::vector<std::shared_ptr<SocketImpl>> active_sockets;
uint16 next_ephemeral_port_offset = 0;

TcpTime CurrentTcpTime() { return ::perception::GetTimeSinceKernelStarted(); }

TcpNetworkLayer LayerForAddress(const IpAddress& addr) {
  return addr.IsV6() ? TcpNetworkLayer::kIpv6 : TcpNetworkLayer::kIpv4;
}

const TcpIsnGenerator& GetIsnGenerator() {
  static const TcpIsnGenerator generator = []() {
    SipHashKey key = kDefaultIsnSeed;
    const uint64 cycles = ::perception::GetClockCyclesSinceBoot();
    for (size_t i = 0; i < sizeof(cycles); ++i)
      key[i] ^= static_cast<uint8>((cycles >> (i * 8)) & 0xFF);
    return TcpIsnGenerator(key);
  }();
  return generator;
}

uint16 AllocateEphemeralPort() {
  uint16 port = static_cast<uint16>(
      kEphemeralPortBase + (next_ephemeral_port_offset % kEphemeralPortCount));
  ++next_ephemeral_port_offset;
  return port;
}

void SendRawTcpSegment(size_t iface_idx, const IpAddress& src_ip,
                       uint16 src_port, const IpAddress& dst_ip,
                       uint16 dst_port, uint32 seq_num, uint32 ack_num,
                       uint8 flags_val, uint16 window_size,
                       std::string_view payload,
                       std::string_view options = {}) {
  IpAddress actual_src = src_ip;
  if (actual_src.IsUnspecified()) {
    if (auto selected = SelectSourceAddress(iface_idx, dst_ip);
        selected.has_value()) {
      actual_src = *selected;
    } else if (NetworkInterface* iface = GetInterface(iface_idx);
               iface != nullptr) {
      auto pref = iface->GetPreferredAddress(dst_ip.family());
      if (!pref.has_value()) return;
      actual_src = *pref;
    } else {
      return;
    }
  }

  const size_t padded_opts_len = (options.size() + 3) & ~static_cast<size_t>(3);
  const size_t header_len = sizeof(TcpHeader) + padded_opts_len;
  const size_t tcp_len = header_len + payload.size();
  std::string tcp_segment(tcp_len, '\0');
  auto* tcp = reinterpret_cast<TcpHeader*>(tcp_segment.data());
  tcp->src_port = Swap16BitEndian(src_port);
  tcp->dest_port = Swap16BitEndian(dst_port);
  tcp->seq_num = Swap32BitEndian(seq_num);
  tcp->ack_num = Swap32BitEndian((flags_val & kTcpFlagAck) ? ack_num : 0);
  tcp->data_offset = static_cast<uint8>((header_len / 4) << 4);
  tcp->flags = flags_val;
  tcp->window_size = Swap16BitEndian(window_size);
  tcp->checksum = 0;
  tcp->urgent_ptr = 0;

  if (!options.empty()) {
    std::memcpy(tcp_segment.data() + sizeof(TcpHeader), options.data(),
                options.size());
  }
  if (!payload.empty()) {
    std::memcpy(tcp_segment.data() + header_len, payload.data(),
                payload.size());
  }

  tcp->checksum = Swap16BitEndian(
      TransportChecksum(actual_src, dst_ip, kProtocolTcp, tcp_segment));

  IpPacketRequest req;
  req.interface_index = iface_idx;
  req.src = actual_src;
  req.dst = dst_ip;
  req.protocol = kProtocolTcp;
  req.payload = tcp_segment;
  req.dont_fragment = true;
  (void)SendIpPacket(req);
}

}  // namespace

void AddActiveSocket(std::shared_ptr<SocketImpl> socket) {
  active_sockets.push_back(std::move(socket));
}

const std::vector<std::shared_ptr<SocketImpl>>& GetActiveSockets() {
  return active_sockets;
}

void SendTcpPacket(size_t iface_idx, std::shared_ptr<SocketImpl> sock,
                   uint8 flags_val, const std::string& payload,
                   const std::string& options) {
  if (!sock) return;
  SendRawTcpSegment(iface_idx, sock->GetLocalEndpoint().address,
                    sock->GetLocalPort(), sock->GetRemoteIp(),
                    sock->GetRemotePort(), sock->GetSeq(), sock->GetAck(),
                    flags_val, sock->GetReceiveWindow(), payload, options);
}

Status SendUdpPacket(size_t iface_idx, const IpAddress& src_ip, uint16 src_port,
                     const IpAddress& dest_ip, uint16 dest_port,
                     const std::string& payload) {
  IpAddress actual_src = src_ip;
  if (actual_src.IsUnspecified()) {
    if (auto selected = SelectSourceAddress(iface_idx, dest_ip);
        selected.has_value()) {
      actual_src = *selected;
    } else if (NetworkInterface* iface = GetInterface(iface_idx);
               iface != nullptr) {
      auto pref = iface->GetPreferredAddress(dest_ip.family());
      if (!pref.has_value()) return Status::INTERNAL_ERROR;
      actual_src = *pref;
    } else {
      return Status::INTERNAL_ERROR;
    }
  }

  const size_t udp_len = sizeof(UdpHeader) + payload.size();
  std::string udp_segment(udp_len, '\0');
  auto* udp = reinterpret_cast<UdpHeader*>(udp_segment.data());
  udp->src_port = Swap16BitEndian(src_port);
  udp->dest_port = Swap16BitEndian(dest_port);
  udp->length = Swap16BitEndian(static_cast<uint16>(udp_len));
  udp->checksum = 0;

  if (!payload.empty()) {
    std::memcpy(udp_segment.data() + sizeof(UdpHeader), payload.data(),
                payload.size());
  }

  uint16 checksum =
      TransportChecksum(actual_src, dest_ip, kProtocolUdp, udp_segment);
  if (checksum == 0) checksum = 0xFFFF;
  udp->checksum = Swap16BitEndian(checksum);

  IpPacketRequest req;
  req.interface_index = iface_idx;
  req.src = actual_src;
  req.dst = dest_ip;
  req.protocol = kProtocolUdp;
  req.payload = udp_segment;
  return SendIpPacket(req);
}

Status SendUdpPacket(size_t iface_idx, uint16 src_port,
                     const IpAddress& dest_ip, uint16 dest_port,
                     const std::string& payload) {
  return SendUdpPacket(iface_idx, IpAddress(), src_port, dest_ip, dest_port,
                       payload);
}

Status SendUdpPacket(size_t iface_idx, const IpAddress& dest_ip,
                     uint16 src_port, uint16 dest_port,
                     const std::string& payload) {
  return SendUdpPacket(iface_idx, IpAddress(), src_port, dest_ip, dest_port,
                       payload);
}

void DispatchUdpPacket(const IpAddress& src_ip, uint16 src_port,
                       const IpAddress& dst_ip, uint16 dest_port,
                       const uint8* payload, size_t len) {
  std::shared_ptr<SocketImpl> best_match;
  const auto sockets = active_sockets;
  for (const auto& sock : sockets) {
    if (sock->GetType() != SocketType::UDP || sock->GetLocalPort() != dest_port)
      continue;
    const IpAddress& bound_ip = sock->GetLocalEndpoint().address;
    if (!bound_ip.IsUnspecified() && !dst_ip.IsUnspecified() &&
        bound_ip != dst_ip)
      continue;
    const IpEndpoint& rem = sock->GetRemoteEndpoint();
    if (!rem.address.IsUnspecified()) {
      if (rem.address != src_ip || (rem.port != 0 && rem.port != src_port))
        continue;
      best_match = sock;
      break;
    }
    if (!best_match) best_match = sock;
  }

  if (best_match) {
    best_match->QueueUdpPacket(
        src_ip, src_port,
        std::string(reinterpret_cast<const char*>(payload), len));
    best_match->WakeBlockedFiber();
  }
}

void DispatchUdpPacket(const IpAddress& src_ip, uint16 src_port,
                       uint16 dest_port, const uint8* payload, size_t len) {
  DispatchUdpPacket(src_ip, src_port, IpAddress(), dest_port, payload, len);
}

void SocketImpl::InitTcpSender(uint32 iss, std::optional<uint16> peer_mss,
                               TcpTime now, bool send_syn) {
  peer_mss_ = peer_mss;
  const TcpNetworkLayer layer = LayerForAddress(remote_.address);
  uint16 path_mtu = GetEffectivePathMtu(remote_.address);
  if (path_mtu == 0) path_mtu = 1500;
  const uint16 initial_mss = EffectiveSendMss(peer_mss_, path_mtu, layer);

  uint16 link_mtu = 1500;
  if (NetworkInterface* iface = GetInterface(iface_idx_); iface != nullptr)
    link_mtu = iface->mtu;
  const uint16 advertised_mss = MssForMtu(link_mtu, layer);

  std::weak_ptr<SocketImpl> weak_self = weak_from_this();
  sender_ = std::make_unique<TcpSender>(
      iss, TcpSenderOptions::Default(initial_mss),
      [weak_self, advertised_mss](const TcpOutgoingSegment& seg) {
        auto self = weak_self.lock();
        if (!self) return;
        uint8 flags = 0;
        std::string opts;
        if (seg.syn) {
          flags = kTcpFlagSyn;
          if (self->state_ == SynReceivedState) flags |= kTcpFlagAck;
          opts = EncodeMssOption(advertised_mss);
        } else {
          flags = kTcpFlagAck;
          if (!seg.payload.empty()) flags |= kTcpFlagPsh;
          if (seg.fin) flags |= kTcpFlagFin;
        }
        std::string payload_copy(seg.payload);
        SendRawTcpSegment(self->iface_idx_, self->local_.address,
                          self->local_.port, self->remote_.address,
                          self->remote_.port, seg.sequence, self->ack_, flags,
                          self->GetReceiveWindow(), payload_copy, opts);
      });

  seq_ = iss;
  if (send_syn) {
    sender_->SendSyn(now);
    seq_ = sender_->SndNxt();
  }
}

Status SocketImpl::Connect(const ConnectRequest& request) {
  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  remote_.address = request.address;
  remote_.port = request.port;
  iface_idx_ = SelectInterfaceForDestination(remote_.address).value_or(0);

  if (local_.port == 0) local_.port = AllocateEphemeralPort();
  if (local_.address.IsUnspecified()) {
    if (auto src = SelectSourceAddress(iface_idx_, remote_.address);
        src.has_value()) {
      local_.address = *src;
    } else if (NetworkInterface* iface = GetInterface(iface_idx_);
               iface != nullptr) {
      if (auto pref = iface->GetPreferredAddress(remote_.address.family());
          pref.has_value()) {
        local_.address = *pref;
      }
    }
  }

  if (type_ == SocketType::UDP) {
    state_ = EstablishedState;
    return Status::OK;
  }

  last_error_ = Status::OK;
  state_ = SynSentState;
  ack_ = 0;

  const TcpTime now = CurrentTcpTime();
  const uint32 iss =
      GetIsnGenerator().Generate(local_.address.bytes(), local_.port,
                                 remote_.address.bytes(), remote_.port, now);
  InitTcpSender(iss, std::nullopt, now, /*send_syn=*/true);

  if (state_ == SynSentState) {
    auto current_fiber = ::perception::GetCurrentlyExecutingFiber();
    blocked_fiber_ = current_fiber;
    auto finished = std::make_shared<bool>(false);

    ::perception::AfterDuration(
        std::chrono::seconds(kTcpConnectTimeoutSeconds),
        [current_fiber, finished]() {
          if (!*finished) {
            *finished = true;
            current_fiber->WakeUp();
          }
        });

    while (state_ == SynSentState && !*finished) {
      ::perception::Sleep();
    }
    *finished = true;
    blocked_fiber_ = nullptr;
  }

  if (state_ == EstablishedState) return Status::OK;
  return (last_error_ != Status::OK) ? last_error_ : Status::INTERNAL_ERROR;
}

Status SocketImpl::Bind(const BindRequest& request) {
  local_.address = request.address;
  local_.port =
      (request.port != 0) ? request.port : AllocateEphemeralPort();
  return Status::OK;
}

Status SocketImpl::Listen() {
  if (local_.port == 0) local_.port = AllocateEphemeralPort();
  state_ = ListenState;
  return Status::OK;
}

StatusOr<AcceptResponse> SocketImpl::Accept() {
  while (accept_queue_.empty()) {
    if (state_ == ClosedState) return Status::INTERNAL_ERROR;
    blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
    ::perception::Sleep();
    blocked_fiber_ = nullptr;
  }

  auto client_sock = accept_queue_.front();
  accept_queue_.erase(accept_queue_.begin());

  AcceptResponse response;
  response.process_id = client_sock->ServerProcessId();
  response.message_id = client_sock->ServiceId();
  response.remote_address = client_sock->GetRemoteIp();
  response.remote_port = client_sock->GetRemotePort();
  return response;
}

Status SocketImpl::Send(const SendRequest& request) {
  if (GetNetworkInterfaceCount() == 0) return Status::INTERNAL_ERROR;

  if (type_ == SocketType::UDP) {
    if (local_.port == 0) local_.port = AllocateEphemeralPort();
    return SendUdpPacket(iface_idx_, local_.address, local_.port,
                         remote_.address, remote_.port, request.data);
  }

  if (state_ != EstablishedState && state_ != CloseWaitState)
    return Status::INTERNAL_ERROR;

  if (sender_) {
    const TcpTime now = CurrentTcpTime();
    std::string_view remaining = request.data;
    while (!remaining.empty()) {
      size_t written = sender_->Write(remaining, now);
      if (written == 0) break;
      remaining.remove_prefix(written);
    }
    seq_ = sender_->SndNxt();
    if (!remaining.empty()) {
      SendTcpPacket(iface_idx_, shared_from_this(), kTcpFlagAck | kTcpFlagPsh,
                    std::string(remaining));
      seq_ += static_cast<uint32>(remaining.size());
    }
    return Status::OK;
  }

  SendTcpPacket(iface_idx_, shared_from_this(), kTcpFlagAck | kTcpFlagPsh,
                request.data);
  seq_ += static_cast<uint32>(request.data.length());
  return Status::OK;
}

StatusOr<ReceiveResponse> SocketImpl::Receive(const ReceiveRequest& request) {
  if (type_ == SocketType::UDP) {
    while (udp_rx_queue_.empty()) {
      if (request.non_blocking) return ReceiveResponse{};
      if (state_ == ClosedState) return Status::INTERNAL_ERROR;
      blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
      ::perception::Sleep();
      blocked_fiber_ = nullptr;
    }

    auto pkt = udp_rx_queue_.front();
    udp_rx_queue_.erase(udp_rx_queue_.begin());

    ReceiveResponse response;
    response.data = pkt.data;
    return response;
  }

  while (rx_buffer_.empty() &&
         (state_ == EstablishedState || state_ == FinWait1State ||
          state_ == FinWait2State)) {
    if (request.non_blocking) {
      ReceiveResponse response;
      response.closed = false;
      return response;
    }
    blocked_fiber_ = ::perception::GetCurrentlyExecutingFiber();
    ::perception::Sleep();
    blocked_fiber_ = nullptr;
  }

  ReceiveResponse response;
  response.closed =
      (state_ != EstablishedState && state_ != FinWait1State &&
       state_ != FinWait2State && rx_buffer_.empty());
  const size_t bytes_to_copy =
      std::min(static_cast<size_t>(request.max_bytes), rx_buffer_.length());
  if (bytes_to_copy > 0) {
    response.data = rx_buffer_.substr(0, bytes_to_copy);
    rx_buffer_ = rx_buffer_.substr(bytes_to_copy);
  }
  return response;
}

Status SocketImpl::Close() {
  if (type_ == SocketType::TCP) {
    const TcpTime now = CurrentTcpTime();
    if (state_ == EstablishedState) {
      state_ = FinWait1State;
      if (sender_) {
        sender_->Close(now);
        seq_ = sender_->SndNxt();
      } else {
        SendTcpPacket(iface_idx_, shared_from_this(), kTcpFlagFin | kTcpFlagAck);
        seq_++;
      }
    } else if (state_ == CloseWaitState) {
      state_ = LastAckState;
      if (sender_) {
        sender_->Close(now);
        seq_ = sender_->SndNxt();
      } else {
        SendTcpPacket(iface_idx_, shared_from_this(), kTcpFlagFin | kTcpFlagAck);
        seq_++;
      }
    } else {
      state_ = ClosedState;
    }
  } else {
    state_ = ClosedState;
  }
  WakeBlockedFiber();
  return Status::OK;
}

StatusOr<SocketEndpoints> SocketImpl::GetEndpoints() {
  SocketEndpoints endpoints;
  endpoints.local_address = local_.address;
  endpoints.local_port = local_.port;
  endpoints.remote_address = remote_.address;
  endpoints.remote_port = remote_.port;
  return endpoints;
}

SocketType SocketImpl::GetType() const { return type_; }
SocketImpl::TcpState SocketImpl::GetState() const { return state_; }
void SocketImpl::SetState(TcpState state) { state_ = state; }
const IpEndpoint& SocketImpl::GetLocalEndpoint() const { return local_; }
void SocketImpl::SetLocalEndpoint(const IpEndpoint& endpoint) {
  local_ = endpoint;
}
const IpEndpoint& SocketImpl::GetRemoteEndpoint() const { return remote_; }
void SocketImpl::SetRemoteEndpoint(const IpEndpoint& endpoint) {
  remote_ = endpoint;
}
uint16 SocketImpl::GetLocalPort() const { return local_.port; }
void SocketImpl::SetLocalPort(uint16 port) { local_.port = port; }
uint16 SocketImpl::GetRemotePort() const { return remote_.port; }
void SocketImpl::SetRemotePort(uint16 port) { remote_.port = port; }
const IpAddress& SocketImpl::GetRemoteIp() const { return remote_.address; }
void SocketImpl::SetRemoteIp(const IpAddress& ip) { remote_.address = ip; }
uint32 SocketImpl::GetSeq() const { return seq_; }
void SocketImpl::SetSeq(uint32 seq) { seq_ = seq; }
uint32 SocketImpl::GetAck() const { return ack_; }
void SocketImpl::SetAck(uint32 ack) { ack_ = ack; }
size_t SocketImpl::GetInterfaceIndex() const { return iface_idx_; }
void SocketImpl::SetInterfaceIndex(size_t iface_idx) {
  iface_idx_ = iface_idx;
}
uint16 SocketImpl::GetReceiveWindow() const {
  if (rx_buffer_.size() >= kDefaultTcpWindow) return 0;
  return static_cast<uint16>(kDefaultTcpWindow - rx_buffer_.size());
}
void SocketImpl::AppendRxBuffer(const std::string& data) { rx_buffer_ += data; }
void SocketImpl::QueueUdpPacket(const IpAddress& src_ip, uint16 src_port,
                                const std::string& data) {
  udp_rx_queue_.push_back({src_ip, src_port, data});
}
void SocketImpl::QueueAcceptedSocket(std::shared_ptr<SocketImpl> socket) {
  accept_queue_.push_back(std::move(socket));
}
::perception::Fiber* SocketImpl::GetBlockedFiber() const {
  return blocked_fiber_;
}
void SocketImpl::SetBlockedFiber(::perception::Fiber* fiber) {
  blocked_fiber_ = fiber;
}
void SocketImpl::WakeBlockedFiber() {
  if (blocked_fiber_ != nullptr) {
    auto* fiber = blocked_fiber_;
    blocked_fiber_ = nullptr;
    fiber->WakeUp();
  }
}
TcpSender* SocketImpl::GetTcpSender() { return sender_.get(); }
void SocketImpl::EnterTimeWait(TcpTime now) {
  state_ = TimeWaitState;
  time_wait_.emplace(now);
}
TcpTimeWait* SocketImpl::GetTimeWait() {
  return time_wait_.has_value() ? &(*time_wait_) : nullptr;
}
Status SocketImpl::GetLastError() const { return last_error_; }
void SocketImpl::SetLastError(Status status) { last_error_ = status; }
void SocketImpl::UpdateEffectiveMssFromPmtu(uint16 new_pmtu) {
  if (!sender_ || new_pmtu == 0) return;
  const uint16 mss = EffectiveSendMss(peer_mss_, new_pmtu,
                                      LayerForAddress(remote_.address));
  sender_->SetMss(mss);
}

void ProcessTcpSegment(size_t iface_idx, const IpAddress& src_ip,
                       const IpAddress& dst_ip, std::string_view tcp_segment) {
  if (tcp_segment.size() < sizeof(TcpHeader)) return;

  const auto* tcp = reinterpret_cast<const TcpHeader*>(tcp_segment.data());
  if (tcp->checksum != 0 &&
      TransportChecksum(src_ip, dst_ip, kProtocolTcp, tcp_segment) != 0) {
    return;
  }

  const uint16 src_port = Swap16BitEndian(tcp->src_port);
  const uint16 dest_port = Swap16BitEndian(tcp->dest_port);
  const uint32 seq = Swap32BitEndian(tcp->seq_num);
  const uint32 ack = Swap32BitEndian(tcp->ack_num);
  const uint8 flags = tcp->flags;
  const uint32 window = Swap16BitEndian(tcp->window_size);

  const size_t tcp_offset = ((tcp->data_offset >> 4) & 0x0F) * 4;
  if (tcp_offset < sizeof(TcpHeader) || tcp_offset > tcp_segment.size()) return;

  std::optional<TcpOptions> parsed_opts;
  if (tcp_offset > sizeof(TcpHeader)) {
    std::span<const uint8> opt_span(
        reinterpret_cast<const uint8*>(tcp_segment.data()) + sizeof(TcpHeader),
        tcp_offset - sizeof(TcpHeader));
    parsed_opts = ParseTcpOptions(opt_span);
  }

  const size_t payload_len = tcp_segment.size() - tcp_offset;
  const uint8* payload =
      reinterpret_cast<const uint8*>(tcp_segment.data()) + tcp_offset;

  TcpSegmentInfo seg_info;
  seg_info.sequence = seq;
  seg_info.acknowledgment = ack;
  seg_info.payload_length = static_cast<uint32>(payload_len);
  seg_info.syn = (flags & kTcpFlagSyn) != 0;
  seg_info.ack = (flags & kTcpFlagAck) != 0;
  seg_info.fin = (flags & kTcpFlagFin) != 0;
  seg_info.rst = (flags & kTcpFlagRst) != 0;

  auto send_rst_reply = [&](const TcpSegmentInfo& info) {
    if (auto rst = BuildResetReply(info); rst.has_value()) {
      const uint8 rst_flags =
          kTcpFlagRst | (rst->ack ? kTcpFlagAck : static_cast<uint8>(0));
      SendRawTcpSegment(iface_idx, dst_ip, dest_port, src_ip, src_port,
                        rst->sequence, rst->acknowledgment, rst_flags, 0, {});
    }
  };

  std::shared_ptr<SocketImpl> best_sock;
  std::shared_ptr<SocketImpl> listener_sock;

  const auto sockets = active_sockets;
  for (const auto& candidate : sockets) {
    if (candidate->GetType() != SocketType::TCP ||
        candidate->GetLocalPort() != dest_port)
      continue;
    const IpAddress& bound_ip = candidate->GetLocalEndpoint().address;
    if (!bound_ip.IsUnspecified() && bound_ip != dst_ip) continue;

    if (candidate->GetState() == SocketImpl::ListenState) {
      if (!listener_sock || !bound_ip.IsUnspecified())
        listener_sock = candidate;
    } else if (candidate->GetState() != SocketImpl::ClosedState &&
               candidate->GetRemoteIp() == src_ip &&
               candidate->GetRemotePort() == src_port) {
      best_sock = candidate;
      break;
    }
  }

  std::shared_ptr<SocketImpl> sock = best_sock ? best_sock : listener_sock;
  if (!sock) {
    send_rst_reply(seg_info);
    return;
  }

  const TcpTime now = CurrentTcpTime();
  sock->SetInterfaceIndex(iface_idx);

  if (sock->GetState() == SocketImpl::ListenState) {
    if (seg_info.rst) return;
    if (seg_info.ack) {
      send_rst_reply(seg_info);
      return;
    }
    if (seg_info.syn) {
      auto new_sock = std::make_shared<SocketImpl>(SocketType::TCP);
      new_sock->SetInterfaceIndex(iface_idx);
      new_sock->SetLocalEndpoint({dst_ip, dest_port});
      new_sock->SetRemoteEndpoint({src_ip, src_port});
      new_sock->SetState(SocketImpl::SynReceivedState);
      new_sock->SetAck(seq + 1);

      const uint32 iss = GetIsnGenerator().Generate(
          dst_ip.bytes(), dest_port, src_ip.bytes(), src_port, now);
      AddActiveSocket(new_sock);
      new_sock->InitTcpSender(
          iss, parsed_opts.has_value() ? parsed_opts->mss : std::nullopt, now,
          /*send_syn=*/true);
    }
    return;
  }

  if (sock->GetState() == SocketImpl::SynSentState) {
    if (seg_info.rst) {
      const uint32 iss =
          sock->GetTcpSender() ? sock->GetTcpSender()->Iss() : sock->GetSeq() - 1;
      if (IsAcceptableRstInSynSent(seg_info, iss, sock->GetSeq())) {
        sock->SetLastError(Status::INTERNAL_ERROR);
        sock->SetState(SocketImpl::ClosedState);
        sock->WakeBlockedFiber();
      }
      return;
    }
    if (seg_info.syn && seg_info.ack) {
      if (auto* sender = sock->GetTcpSender()) {
        if (sender->OnSegment(seg_info, window, now) ==
            TcpAckResult::kUnsentData) {
          send_rst_reply(seg_info);
          return;
        }
        if (parsed_opts.has_value() && parsed_opts->mss.has_value()) {
          uint16 pmtu = GetEffectivePathMtu(src_ip);
          if (pmtu == 0) pmtu = 1500;
          sender->SetMss(EffectiveSendMss(parsed_opts->mss, pmtu,
                                          LayerForAddress(src_ip)));
        }
        sock->SetSeq(sender->SndNxt());
      }
      sock->SetAck(seq + 1);
      sock->SetState(SocketImpl::EstablishedState);
      SendTcpPacket(iface_idx, sock, kTcpFlagAck);
      sock->WakeBlockedFiber();
    } else if (seg_info.syn && !seg_info.ack) {
      sock->SetAck(seq + 1);
      sock->SetState(SocketImpl::SynReceivedState);
      SendTcpPacket(iface_idx, sock, kTcpFlagSyn | kTcpFlagAck);
    }
    return;
  }

  if (!IsSegmentAcceptable(seg_info, sock->GetAck(),
                           sock->GetReceiveWindow())) {
    if (!seg_info.rst) SendTcpPacket(iface_idx, sock, kTcpFlagAck);
    return;
  }

  if (seg_info.rst) {
    const RstAction action =
        ValidateRst(seg_info.sequence, sock->GetAck(), sock->GetReceiveWindow());
    if (action == RstAction::kChallengeAck) {
      SendTcpPacket(iface_idx, sock, kTcpFlagAck);
    } else if (action == RstAction::kReset) {
      sock->SetLastError(Status::INTERNAL_ERROR);
      sock->SetState(SocketImpl::ClosedState);
      sock->WakeBlockedFiber();
    }
    return;
  }

  if (sock->GetState() == SocketImpl::TimeWaitState) {
    if (auto* tw = sock->GetTimeWait()) {
      if (tw->OnSegment(seg_info.fin, now)) {
        sock->SetAck(seg_info.sequence + seg_info.payload_length + 1);
        SendTcpPacket(iface_idx, sock, kTcpFlagAck);
      }
    }
    return;
  }

  if (auto* sender = sock->GetTcpSender()) {
    const TcpAckResult ack_res = sender->OnSegment(seg_info, window, now);
    if (ack_res == TcpAckResult::kUnsentData) {
      if (sock->GetState() == SocketImpl::SynReceivedState)
        send_rst_reply(seg_info);
      else
        SendTcpPacket(iface_idx, sock, kTcpFlagAck);
      return;
    }
    sock->SetSeq(sender->SndNxt());
  }

  if (sock->GetState() == SocketImpl::SynReceivedState) {
    if (seg_info.ack) {
      sock->SetState(SocketImpl::EstablishedState);
      const auto current_sockets = active_sockets;
      for (const auto& parent : current_sockets) {
        if (parent->GetType() != SocketType::TCP ||
            parent->GetState() != SocketImpl::ListenState ||
            parent->GetLocalPort() != dest_port)
          continue;
        const IpAddress& bound_ip = parent->GetLocalEndpoint().address;
        if (!bound_ip.IsUnspecified() && bound_ip != dst_ip) continue;
        parent->QueueAcceptedSocket(sock);
        parent->WakeBlockedFiber();
        break;
      }
    }
    return;
  }

  if (sock->GetState() == SocketImpl::LastAckState) {
    if (seg_info.ack) {
      sock->SetState(SocketImpl::ClosedState);
      sock->WakeBlockedFiber();
    }
    return;
  }

  if (sock->GetState() == SocketImpl::FinWait1State && seg_info.ack) {
    if (!sock->GetTcpSender() || sock->GetTcpSender()->IsFinAcknowledged())
      sock->SetState(SocketImpl::FinWait2State);
  }

  bool sent_ack = false;
  if (payload_len > 0 &&
      (sock->GetState() == SocketImpl::EstablishedState ||
       sock->GetState() == SocketImpl::FinWait1State ||
       sock->GetState() == SocketImpl::FinWait2State)) {
    if (seq == sock->GetAck()) {
      sock->AppendRxBuffer(
          std::string(reinterpret_cast<const char*>(payload), payload_len));
      sock->SetAck(seq + static_cast<uint32>(payload_len));
      SendTcpPacket(iface_idx, sock, kTcpFlagAck);
      sent_ack = true;
      sock->WakeBlockedFiber();
    } else {
      SendTcpPacket(iface_idx, sock, kTcpFlagAck);
      sent_ack = true;
    }
  }

  if (seg_info.fin) {
    sock->SetAck(seq + static_cast<uint32>(payload_len) + 1);
    if (!sent_ack) SendTcpPacket(iface_idx, sock, kTcpFlagAck);
    if (sock->GetState() == SocketImpl::EstablishedState) {
      sock->SetState(SocketImpl::CloseWaitState);
    } else if (sock->GetState() == SocketImpl::FinWait1State) {
      if (!sock->GetTcpSender() || sock->GetTcpSender()->IsFinAcknowledged())
        sock->EnterTimeWait(now);
      else
        sock->SetState(SocketImpl::LastAckState);
    } else if (sock->GetState() == SocketImpl::FinWait2State) {
      sock->EnterTimeWait(now);
    }
    sock->WakeBlockedFiber();
  }
}

void NotifySocketIcmpError(const IpAddress& src_ip, const IpAddress& dst_ip,
                           uint8 inner_protocol,
                           std::string_view inner_transport_header,
                           Status error_status, uint16 new_pmtu) {
  if (inner_transport_header.size() < 4) return;
  const uint8* bytes =
      reinterpret_cast<const uint8*>(inner_transport_header.data());
  const uint16 src_port =
      static_cast<uint16>((bytes[0] << 8) | bytes[1]);
  const uint16 dst_port =
      static_cast<uint16>((bytes[2] << 8) | bytes[3]);

  const SocketType target_type =
      (inner_protocol == kProtocolTcp) ? SocketType::TCP : SocketType::UDP;

  const auto sockets = active_sockets;
  for (const auto& sock : sockets) {
    if (sock->GetType() != target_type) continue;
    if (sock->GetLocalPort() != src_port) continue;
    if (sock->GetRemotePort() != 0 && sock->GetRemotePort() != dst_port)
      continue;
    if (!sock->GetRemoteIp().IsUnspecified() && sock->GetRemoteIp() != dst_ip)
      continue;
    const IpAddress& bound_ip = sock->GetLocalEndpoint().address;
    if (!bound_ip.IsUnspecified() && !src_ip.IsUnspecified() &&
        bound_ip != src_ip)
      continue;

    if (new_pmtu > 0 && target_type == SocketType::TCP)
      sock->UpdateEffectiveMssFromPmtu(new_pmtu);

    if (error_status != Status::OK) {
      sock->SetLastError(error_status);
      if (target_type == SocketType::TCP &&
          sock->GetState() == SocketImpl::SynSentState) {
        sock->SetState(SocketImpl::ClosedState);
        sock->WakeBlockedFiber();
      }
    }
  }
}

void TickTcpSockets() {
  const TcpTime now = CurrentTcpTime();
  const auto sockets = active_sockets;
  for (const auto& sock : sockets) {
    if (sock->GetType() != SocketType::TCP) continue;
    if (sock->GetState() == SocketImpl::TimeWaitState) {
      if (auto* tw = sock->GetTimeWait(); tw != nullptr && tw->IsExpired(now)) {
        sock->SetState(SocketImpl::ClosedState);
        sock->WakeBlockedFiber();
      }
      continue;
    }
    if (sock->GetState() == SocketImpl::ClosedState ||
        sock->GetState() == SocketImpl::ListenState)
      continue;
    if (auto* sender = sock->GetTcpSender()) {
      if (sender->OnTimer(now) == TcpTimerResult::kAborted) {
        sock->SetLastError(Status::INTERNAL_ERROR);
        sock->SetState(SocketImpl::ClosedState);
        sock->WakeBlockedFiber();
      } else {
        sock->SetSeq(sender->SndNxt());
      }
    }
  }
}
