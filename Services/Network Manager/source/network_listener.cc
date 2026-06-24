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

#include "network_listener.h"

#include <iostream>
#include <memory>
#include <string>

#include "dns.h"
#include "endian.h"
#include "interface.h"
#include "protocols.h"
#include "socket.h"

namespace {

std::vector<std::shared_ptr<NetworkListener>> listeners;

void SendIcmpEchoRequest(size_t iface_idx, uint32 dest_ip) {
  auto& iface = GetNetworkInterface(iface_idx);
  bool is_external = (dest_ip & 0x00FFFFFF) != (iface.ip & 0x00FFFFFF);
  if (is_external && !iface.gateway_mac_resolved) {
    SendArpRequest(iface_idx, iface.gateway_ip);
    while (!iface.gateway_mac_resolved) {
      WaitForArp(iface_idx);
    }
  }

  size_t ip_payload_len = sizeof(IcmpHeader);
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
  ip->id = Swap16BitEndian(9999);
  ip->flags_offset = 0;
  ip->ttl = 64;
  ip->protocol = 1;  // ICMP
  ip->src_ip = iface.ip;
  ip->dest_ip = dest_ip;
  ip->checksum = 0;
  ip->checksum = CalculateChecksum((const uint16*)ip, sizeof(IpHeader));

  IcmpHeader* icmp = (IcmpHeader*)(packet_data.data() + sizeof(EthernetHeader) +
                                   sizeof(IpHeader));
  icmp->type = 8;  // Echo Request
  icmp->code = 0;
  icmp->checksum = 0;
  icmp->id = Swap16BitEndian(0x1234);
  icmp->seq = Swap16BitEndian(1);
  icmp->checksum = CalculateChecksum((const uint16*)icmp, sizeof(IcmpHeader));

  ::perception::devices::Packet pkt;
  pkt.data = packet_data;
  iface.device.SendPacket(pkt);
}

void ProcessArp(const std::string& data, size_t iface_idx) {
  if (data.length() < sizeof(EthernetHeader) + sizeof(ArpHeader)) return;

  const ArpHeader* arp =
      (const ArpHeader*)(data.data() + sizeof(EthernetHeader));
  uint16 oper = Swap16BitEndian(arp->oper);
  auto& iface = GetNetworkInterface(iface_idx);

  if (oper == 1) {
    if (arp->tpa == iface.ip) SendArpReply(iface_idx, arp->sha, arp->spa);
  } else if (oper == 2) {
    if (arp->spa == iface.gateway_ip) {
      for (int i = 0; i < 6; i++) {
        iface.gateway_mac[i] = arp->sha[i];
      }
      iface.gateway_mac_resolved = true;
      WakeFibersWaitingForArp(iface_idx);
    } else if (arp->spa == iface.gateway_ip + Swap32BitEndian(1)) {
      ::perception::devices::MacAddress mac;
      for (int i = 0; i < 6; i++) mac.mac[i] = arp->sha[i];
      SetDnsMac(mac);
      WakeFibersWaitingForArp(iface_idx);
    }
  }
}

void ProcessIcmp(const std::string& data, uint8 ihl, size_t iface_idx) {
  if (data.length() < sizeof(EthernetHeader) + ihl + sizeof(IcmpHeader)) return;

  const IpHeader* ip = (const IpHeader*)(data.data() + sizeof(EthernetHeader));
  const IcmpHeader* icmp =
      (const IcmpHeader*)(data.data() + sizeof(EthernetHeader) + ihl);

  if (icmp->type == 8) {
    std::string reply_data = data;
    auto& iface = GetNetworkInterface(iface_idx);

    EthernetHeader* eth = (EthernetHeader*)reply_data.data();
    for (int i = 0; i < 6; i++) {
      eth->dest_mac[i] = eth->src_mac[i];
      eth->src_mac[i] = iface.mac[i];
    }

    IpHeader* reply_ip =
        (IpHeader*)(reply_data.data() + sizeof(EthernetHeader));
    reply_ip->dest_ip = ip->src_ip;
    reply_ip->src_ip = iface.ip;
    reply_ip->checksum = 0;
    reply_ip->checksum = CalculateChecksum((const uint16*)reply_ip, ihl);

    IcmpHeader* reply_icmp =
        (IcmpHeader*)(reply_data.data() + sizeof(EthernetHeader) + ihl);
    reply_icmp->type = 0;
    reply_icmp->checksum = 0;

    size_t icmp_len = data.length() - sizeof(EthernetHeader) - ihl;
    reply_icmp->checksum =
        CalculateChecksum((const uint16*)reply_icmp, icmp_len);

    ::perception::devices::Packet pkt;
    pkt.data = reply_data;
    iface.device.SendPacket(pkt);
  }
}

void ProcessUdp(const std::string& data, uint8 ihl, size_t iface_idx) {
  if (data.length() < sizeof(EthernetHeader) + ihl + sizeof(UdpHeader)) return;

  const IpHeader* ip = (const IpHeader*)(data.data() + sizeof(EthernetHeader));
  const UdpHeader* udp =
      (const UdpHeader*)(data.data() + sizeof(EthernetHeader) + ihl);

  uint16 src_port = Swap16BitEndian(udp->src_port);
  uint16 dest_port = Swap16BitEndian(udp->dest_port);

  uint16 len = Swap16BitEndian(udp->len);

  if (data.length() < sizeof(EthernetHeader) + ihl + len) return;

  const uint8* payload = (const uint8*)data.data() + sizeof(EthernetHeader) +
                         ihl + sizeof(UdpHeader);
  size_t payload_len = len - sizeof(UdpHeader);
  auto& iface = GetNetworkInterface(iface_idx);

  if (src_port == 53 && (ip->src_ip == iface.gateway_ip ||
                         ip->src_ip == iface.gateway_ip + Swap32BitEndian(1) ||
                         ip->src_ip == 0x08080808)) {
    ProcessDnsResponse(payload, payload_len);
    return;
  }

  DispatchUdpPacket(ip->src_ip, src_port, dest_port, payload, payload_len);
}

void ProcessTcp(const std::string& data, uint8 ihl, size_t iface_idx) {
  if (data.length() < sizeof(EthernetHeader) + ihl + sizeof(TcpHeader)) return;

  const IpHeader* ip = (const IpHeader*)(data.data() + sizeof(EthernetHeader));
  const TcpHeader* tcp =
      (const TcpHeader*)(data.data() + sizeof(EthernetHeader) + ihl);

  uint16 src_port = Swap16BitEndian(tcp->src_port);
  uint16 dest_port = Swap16BitEndian(tcp->dest_port);
  uint32 seq = Swap32BitEndian(tcp->seq);
  uint32 ack = Swap32BitEndian(tcp->ack);
  uint16 flags = Swap16BitEndian(tcp->flags);

  uint8 tcp_offset = ((flags >> 12) & 0x0F) * 4;
  size_t ip_logical_len = Swap16BitEndian(ip->len);
  if (ip_logical_len < ihl + tcp_offset) {
    std::cout << "Network Manager: ProcessTcp: Packet logical length too small "
                 "for headers: "
              << ip_logical_len << " < " << (ihl + tcp_offset) << std::endl;
    return;
  }

  size_t payload_len = ip_logical_len - ihl - tcp_offset;
  const uint8* payload =
      (const uint8*)data.data() + sizeof(EthernetHeader) + ihl + tcp_offset;

  std::shared_ptr<SocketImpl> best_sock = nullptr;
  std::shared_ptr<SocketImpl> listener_sock = nullptr;

  for (auto& sock : GetActiveSockets()) {
    if (sock->GetType() == SocketType::TCP &&
        sock->GetLocalPort() == dest_port) {
      if (sock->GetState() == SocketImpl::ListenState) {
        listener_sock = sock;
      } else if (sock->GetRemoteIp() == ip->src_ip &&
                 sock->GetRemotePort() == src_port) {
        best_sock = sock;
        break;
      }
    }
  }

  std::shared_ptr<SocketImpl> sock = best_sock ? best_sock : listener_sock;
  if (!sock) return;

  bool syn = (flags & 0x02) != 0;
  bool ack_flag = (flags & 0x10) != 0;
  bool fin = (flags & 0x01) != 0;
  bool rst = (flags & 0x04) != 0;

  if (sock->GetState() == SocketImpl::ListenState) {
    if (syn) {
      auto new_sock = std::make_shared<SocketImpl>(SocketType::TCP);
      new_sock->SetLocalPort(dest_port);
      new_sock->SetRemotePort(src_port);
      new_sock->SetRemoteIp(ip->src_ip);
      new_sock->SetState(SocketImpl::SynReceivedState);
      new_sock->SetSeq(2000);
      new_sock->SetAck(seq + 1);

      AddActiveSocket(new_sock);
      SendTcpPacket(iface_idx, new_sock, 0x12);
      new_sock->SetSeq(new_sock->GetSeq() + 1);
    }
    return;
  }

  if (sock->GetState() == SocketImpl::SynSentState) {
    if (syn && ack_flag) {
      sock->SetAck(seq + 1);
      sock->SetState(SocketImpl::EstablishedState);
      SendTcpPacket(iface_idx, sock, 0x10);

      if (sock->GetBlockedFiber()) {
        sock->GetBlockedFiber()->WakeUp();
        sock->SetBlockedFiber(nullptr);
      }
    }
    return;
  }

  if (sock->GetState() == SocketImpl::SynReceivedState) {
    if (ack_flag) {
      sock->SetState(SocketImpl::EstablishedState);

      for (auto& parent : GetActiveSockets()) {
        if (parent->GetType() == SocketType::TCP &&
            parent->GetLocalPort() == dest_port &&
            parent->GetState() == SocketImpl::ListenState) {
          parent->QueueAcceptedSocket(sock);
          if (parent->GetBlockedFiber()) {
            parent->GetBlockedFiber()->WakeUp();
            parent->SetBlockedFiber(nullptr);
          }
          break;
        }
      }
    }
    return;
  }

  if (sock->GetState() == SocketImpl::EstablishedState) {
    if (rst) {
      sock->SetState(SocketImpl::ClosedState);
      if (sock->GetBlockedFiber()) {
        sock->GetBlockedFiber()->WakeUp();
        sock->SetBlockedFiber(nullptr);
      }
      return;
    }

    if (payload_len > 0) {
      sock->AppendRxBuffer(std::string((const char*)payload, payload_len));
      sock->SetAck(seq + payload_len);
      SendTcpPacket(iface_idx, sock, 0x10);

      if (sock->GetBlockedFiber()) {
        sock->GetBlockedFiber()->WakeUp();
        sock->SetBlockedFiber(nullptr);
      }
    }

    if (fin) {
      sock->SetAck(seq + payload_len + 1);
      SendTcpPacket(iface_idx, sock, 0x10);
      sock->SetState(SocketImpl::CloseWaitState);

      if (sock->GetBlockedFiber()) {
        sock->GetBlockedFiber()->WakeUp();
        sock->SetBlockedFiber(nullptr);
      }
    }
    return;
  }

  if (sock->GetState() == SocketImpl::LastAckState) {
    if (ack_flag) {
      sock->SetState(SocketImpl::ClosedState);
    }
    return;
  }

  if (sock->GetState() == SocketImpl::FinWait1State) {
    if (ack_flag) {
      sock->SetState(SocketImpl::FinWait2State);
    }
    if (fin) {
      sock->SetAck(seq + payload_len + 1);
      SendTcpPacket(iface_idx, sock, 0x10);
      sock->SetState(SocketImpl::ClosedState);
    }
    return;
  }
}

void ProcessIp(const std::string& data, size_t iface_idx) {
  if (data.length() < sizeof(EthernetHeader) + sizeof(IpHeader)) return;

  const IpHeader* ip = (const IpHeader*)(data.data() + sizeof(EthernetHeader));
  uint8 ihl = (ip->version_ihl & 0x0F) * 4;
  if (data.length() < sizeof(EthernetHeader) + ihl) return;

  if (ip->dest_ip != GetNetworkInterface(iface_idx).ip &&
      ip->dest_ip != 0xFFFFFFFF)
    return;

  if (ip->protocol == 1) {
    ProcessIcmp(data, ihl, iface_idx);
  } else if (ip->protocol == 17) {
    ProcessUdp(data, ihl, iface_idx);
  } else if (ip->protocol == 6) {
    ProcessTcp(data, ihl, iface_idx);
  }
}

}  // namespace

NetworkListener::NetworkListener(size_t interface_index)
    : ::perception::devices::NetworkListener::Server(),
      interface_index_(interface_index) {}

Status NetworkListener::PacketReceived(
    const ::perception::devices::Packet& packet) {
  if (packet.data.length() < sizeof(EthernetHeader)) return Status::OK;

  const EthernetHeader* eth = (const EthernetHeader*)packet.data.data();
  uint16 eth_type = Swap16BitEndian(eth->type);

  if (eth_type == 0x0806) {
    ProcessArp(packet.data, interface_index_);
  } else if (eth_type == 0x0800) {
    ProcessIp(packet.data, interface_index_);
  }
  return Status::OK;
}

void CreateAndAddNetworkListener(size_t interface_index) {
  auto listener = std::make_shared<NetworkListener>(interface_index);
  listeners.push_back(listener);
  GetNetworkInterface(interface_index)
      .device.SetPacketListener(
          ::perception::devices::NetworkListener::Client(*listener));
}
