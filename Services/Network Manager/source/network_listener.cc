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

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "checksum.h"
#include "dhcpv4.h"
#include "dhcpv6.h"
#include "dns.h"
#include "endian.h"
#include "ethernet.h"
#include "firewall.h"
#include "forwarding.h"
#include "icmpv6.h"
#include "interface.h"
#include "ip.h"
#include "ipsec.h"
#include "ipv6_header.h"
#include "mipv6.h"
#include "nat64_clat.h"
#include "perception/scheduler.h"
#include "protocols.h"
#include "reassembly.h"
#include "socket.h"

// Cross-module entry points implemented in socket.cc.
void DispatchUdpPacket(const ::perception::network::IpAddress& src_ip,
                       uint16 src_port,
                       const ::perception::network::IpAddress& dst_ip,
                       uint16 dest_port, const uint8* payload, size_t len);
void ProcessTcpSegment(size_t iface_idx,
                       const ::perception::network::IpAddress& src_ip,
                       const ::perception::network::IpAddress& dst_ip,
                       std::string_view tcp_segment);
void NotifySocketIcmpError(const ::perception::network::IpAddress& src_ip,
                           const ::perception::network::IpAddress& dst_ip,
                           uint8 inner_proto, std::string_view inner_transport,
                           Status status, uint16 new_pmtu);

namespace {

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

// Hardware type for Ethernet in ARP packets (1).
constexpr uint16 kArpHtypeEthernet = 1;

// Hardware address length for Ethernet in ARP packets (6).
constexpr uint8 kArpHlenEthernet = 6;

// Protocol address length for IPv4 in ARP packets (4).
constexpr uint8 kArpPlenIpv4 = 4;

// ARP operation code for Request (1).
constexpr uint16 kArpOperRequest = 1;

// ARP operation code for Reply (2).
constexpr uint16 kArpOperReply = 2;

// IP protocol number for ICMPv4 (1).
constexpr uint8 kProtocolIcmpv4 = 1;

// IP protocol number for TCP (6).
constexpr uint8 kProtocolTcp = 6;

// IP protocol number for UDP (17).
constexpr uint8 kProtocolUdp = 17;

// IP protocol number for ICMPv6 (58).
constexpr uint8 kProtocolIcmpv6 = 58;

// ICMPv4 Echo Reply type (0).
constexpr uint8 kIcmpEchoReply = 0;

// ICMPv4 Destination Unreachable type (3).
constexpr uint8 kIcmpDestUnreachable = 3;

// ICMPv4 Fragmentation Needed code (4).
constexpr uint8 kIcmpFragNeededCode = 4;

// ICMPv4 Echo Request type (8).
constexpr uint8 kIcmpEchoRequest = 8;

// ICMPv4 Time Exceeded type (11).
constexpr uint8 kIcmpTimeExceeded = 11;

// Standard DNS server port (53).
constexpr uint16 kDnsServerPort = 53;

// IPv4 more-fragments flag mask in fragment_offset field (0x2000).
constexpr uint16 kIpv4MoreFragmentsMask = 0x2000;

// IPv4 fragment offset 8-byte block mask (0x1FFF).
constexpr uint16 kIpv4FragmentOffsetMask = 0x1FFF;

std::vector<std::shared_ptr<NetworkListener>> listeners;

void ProcessArp(std::string_view payload, size_t iface_idx) {
  if (payload.size() < sizeof(ArpHeader)) return;

  const auto* arp = reinterpret_cast<const ArpHeader*>(payload.data());
  if (Swap16BitEndian(arp->htype) != kArpHtypeEthernet ||
      Swap16BitEndian(arp->ptype) != kEtherTypeIpv4 ||
      arp->hlen != kArpHlenEthernet || arp->plen != kArpPlenIpv4) {
    return;
  }

  uint16 oper = Swap16BitEndian(arp->oper);
  std::array<uint8, 6> sender_mac{};
  std::memcpy(sender_mac.data(), arp->sha, 6);
  IpAddress sender_ip =
      IpAddress::FromBytes(IpAddressFamily::V4, {arp->spa, 4});
  IpAddress target_ip =
      IpAddress::FromBytes(IpAddressFamily::V4, {arp->tpa, 4});

  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  if (!sender_ip.IsUnspecified()) {
    if (oper == kArpOperReply) {
      RecordNeighborReachable(iface_idx, sender_ip, sender_mac);
    } else if (oper == kArpOperRequest) {
      RecordUnsolicitedNeighbor(iface_idx, sender_ip, sender_mac, true);
    }
  }

  ProcessIncomingArpForDhcpv4(iface_idx, sender_ip, target_ip, sender_mac);

  if (oper == kArpOperRequest && iface->HasAddress(target_ip))
    SendArpReply(iface_idx, sender_mac, sender_ip);
}

void ProcessIcmpv4(const IpPacketView& ip_view, size_t iface_idx) {
  if (ip_view.payload.size() < sizeof(IcmpHeader)) return;
  if (InternetChecksum(ip_view.payload) != 0) return;

  const auto* icmp = reinterpret_cast<const IcmpHeader*>(ip_view.payload.data());
  if (icmp->type == kIcmpEchoRequest) {
    std::string reply(ip_view.payload.data(), ip_view.payload.size());
    auto* reply_icmp = reinterpret_cast<IcmpHeader*>(reply.data());
    reply_icmp->type = kIcmpEchoReply;
    reply_icmp->code = 0;
    reply_icmp->checksum = 0;
    reply_icmp->checksum = Swap16BitEndian(InternetChecksum(reply));

    IpPacketRequest req;
    req.interface_index = iface_idx;
    req.src = ip_view.dst;
    req.dst = ip_view.src;
    req.protocol = kProtocolIcmpv4;
    req.payload = reply;
    (void)SendIpPacket(req);
    return;
  }

  if (icmp->type == kIcmpDestUnreachable || icmp->type == kIcmpTimeExceeded) {
    std::string_view quoted = ip_view.payload.substr(sizeof(IcmpHeader));
    if (quoted.size() < sizeof(IpHeader)) return;
    const auto* raw_inner = reinterpret_cast<const IpHeader*>(quoted.data());
    if ((Swap16BitEndian(raw_inner->flags_fragment) & kIpv4FragmentOffsetMask) !=
        0)
      return;
    auto inner = ParseIpv4Packet(quoted);
    if (!inner.has_value()) return;
    uint16 new_pmtu = 0;
    if (icmp->type == kIcmpDestUnreachable &&
        icmp->code == kIcmpFragNeededCode) {
      new_pmtu = Swap16BitEndian(icmp->sequence);
      UpdatePathMtu(inner->dst, new_pmtu);
    }
    NotifySocketIcmpError(inner->src, inner->dst, inner->protocol,
                          inner->payload, Status::MISSING_MEDIA, new_pmtu);
  }
}

void ProcessIcmpv6(const IpAddress& src_ip, const IpAddress& dst_ip,
                   uint8 hop_limit, bool has_router_alert,
                   std::string_view payload, size_t iface_idx) {
  if (payload.size() < 4) return;
  if (!VerifyIcmpv6Checksum(src_ip, dst_ip, payload)) return;

  const uint8 raw_type = static_cast<uint8>(payload[0]);
  if (raw_type == static_cast<uint8>(Icmpv6Type::EchoRequest)) {
    auto echo = ParseIcmpv6Echo(payload);
    if (!echo.has_value() || echo->is_reply) return;
    NetworkInterface* iface = GetInterface(iface_idx);
    if (iface == nullptr) return;
    IpAddress reply_src = dst_ip;
    if (dst_ip.IsMulticast()) {
      auto selected = SelectSourceAddress(iface_idx, src_ip);
      if (!selected.has_value()) return;
      reply_src = *selected;
    }
    Icmpv6EchoMessage reply_msg = *echo;
    reply_msg.is_reply = true;
    std::string reply = BuildIcmpv6Echo(reply_src, src_ip, reply_msg);
    IpPacketRequest req;
    req.interface_index = iface_idx;
    req.src = reply_src;
    req.dst = src_ip;
    req.protocol = kProtocolIcmpv6;
    req.payload = reply;
    (void)SendIpPacket(req);
    return;
  }

  if (raw_type >= 133 && raw_type <= 137) {
    ProcessIncomingNdpPacket(iface_idx, src_ip, dst_ip, hop_limit, payload);
    return;
  }

  if (raw_type == static_cast<uint8>(Icmpv6Type::MulticastListenerQuery)) {
    ProcessIncomingMldQuery(iface_idx, src_ip, hop_limit, has_router_alert,
                            payload);
    return;
  }

  if (IsIcmpv6ErrorMessage(raw_type)) {
    auto err = ParseIcmpv6Error(payload);
    if (!err.has_value() || !err->invoking_header.has_value()) return;
    uint16 new_pmtu = 0;
    if (err->type == Icmpv6Type::PacketTooBig) {
      if (err->parameter >= 1280 && err->parameter <= 65535) {
        new_pmtu = static_cast<uint16>(err->parameter);
        UpdatePathMtu(err->invoking_header->destination, new_pmtu);
      }
    }
    ExtensionWalkResult inner_walk =
        WalkExtensionHeaders(*err->invoking_header, err->invoking_packet);
    if (inner_walk.status == ExtensionWalkStatus::UpperLayer) {
      NotifySocketIcmpError(err->invoking_header->source,
                            err->invoking_header->destination,
                            inner_walk.next_header, inner_walk.payload,
                            Status::MISSING_MEDIA, new_pmtu);
    }
  }
}

void ProcessUdp(size_t iface_idx, const IpAddress& src_ip,
                const IpAddress& dst_ip, std::string_view udp_segment) {
  if (udp_segment.size() < sizeof(UdpHeader)) return;

  const auto* udp = reinterpret_cast<const UdpHeader*>(udp_segment.data());
  uint16 src_port = Swap16BitEndian(udp->src_port);
  uint16 dest_port = Swap16BitEndian(udp->dest_port);
  uint16 len = Swap16BitEndian(udp->length);

  if (len < sizeof(UdpHeader) || len > udp_segment.size()) return;

  // RFC 8200 §8.1: IPv6 UDP packets with a zero checksum must be discarded.
  if (src_ip.IsV6()) {
    if (udp->checksum == 0) return;
    if (TransportChecksum(src_ip, dst_ip, kProtocolUdp,
                          udp_segment.substr(0, len)) != 0)
      return;
  } else if (udp->checksum != 0 && !src_ip.IsUnspecified() &&
             !dst_ip.IsBroadcast()) {
    if (TransportChecksum(src_ip, dst_ip, kProtocolUdp,
                          udp_segment.substr(0, len)) != 0)
      return;
  }

  std::string_view body =
      udp_segment.substr(sizeof(UdpHeader), len - sizeof(UdpHeader));
  const auto* payload = reinterpret_cast<const uint8*>(body.data());
  const size_t payload_len = body.size();

  if (src_ip.IsV4() && dest_port == kDhcpv4ClientPort) {
    ProcessIncomingDhcpv4Packet(iface_idx, body);
    return;
  }

  if (src_ip.IsV6() && dest_port == kDhcpv6ClientPort) {
    ProcessIncomingDhcpv6Packet(iface_idx, body);
    return;
  }

  if (src_port == kDnsServerPort) {
    ProcessDnsResponse(payload, payload_len);
  }

  DispatchUdpPacket(src_ip, src_port, dst_ip, dest_port, payload, payload_len);
}

bool IsInboundPermittedByFirewall(
    const FirewallPacket& fw_pkt,
    std::chrono::steady_clock::time_point now) {
  if (GetFirewall().Evaluate(fw_pkt, now) == FirewallAction::Allow) return true;

  for (const FirewallRule& rule : GetFirewall().rules()) {
    if (rule.Matches(fw_pkt)) return rule.action == FirewallAction::Allow;
  }

  if (fw_pkt.protocol == kProtocolUdp) {
    if (fw_pkt.source.IsV4() && fw_pkt.dst_port == kDhcpv4ClientPort)
      return true;
    if (fw_pkt.source.IsV6() && fw_pkt.dst_port == kDhcpv6ClientPort)
      return true;
    if (fw_pkt.src_port == kDnsServerPort) return true;
  }

  if (fw_pkt.protocol == kProtocolIcmpv4 &&
      fw_pkt.icmp_type == kIcmpEchoRequest)
    return true;
  if (fw_pkt.protocol == kProtocolIcmpv6 &&
      fw_pkt.icmp_type == static_cast<uint8>(Icmpv6Type::EchoRequest))
    return true;

  if (fw_pkt.protocol == kProtocolTcp || fw_pkt.protocol == kProtocolUdp) {
    const SocketType target_type =
        (fw_pkt.protocol == kProtocolTcp) ? SocketType::TCP : SocketType::UDP;
    for (const auto& sock : GetActiveSockets()) {
      if (!sock || sock->GetType() != target_type) continue;
      if (sock->GetLocalPort() == 0 || sock->GetLocalPort() != fw_pkt.dst_port)
        continue;
      if (target_type == SocketType::TCP &&
          sock->GetState() == SocketImpl::ClosedState)
        continue;
      const IpAddress& bound_ip = sock->GetLocalEndpoint().address;
      if (!bound_ip.IsUnspecified() && bound_ip != fw_pkt.destination) continue;
      return true;
    }
  }
  return false;
}

std::vector<ForwardingInterface> BuildForwardingInterfaces() {
  std::vector<ForwardingInterface> result;
  const auto& nics = GetNetworkInterfaces();
  for (size_t i = 0; i < nics.size(); ++i) {
    ForwardingInterface fi;
    fi.index = i;
    fi.mtu = nics[i].mtu;
    for (const InterfaceAddress& a : nics[i].addresses) {
      if (a.state != AddressState::Duplicate) fi.addresses.push_back(a.address);
    }
    result.push_back(std::move(fi));
  }
  return result;
}

void ProcessIpv4(std::string_view frame_payload, size_t iface_idx) {
  const auto now = std::chrono::steady_clock::now();
  IpsecResult ipsec_res = GetIpsecEngine().ProcessInbound(frame_payload);
  if (ipsec_res.status == IpsecStatus::Dropped) return;
  std::string_view packet = (ipsec_res.status == IpsecStatus::Protected)
                                ? std::string_view(ipsec_res.packet)
                                : frame_payload;

  auto ip_view = ParseIpv4Packet(packet);
  if (!ip_view.has_value()) return;

  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  const bool is_dhcpv4 =
      ip_view->protocol == kProtocolUdp &&
      ip_view->payload.size() >= sizeof(UdpHeader) &&
      Swap16BitEndian(
          reinterpret_cast<const UdpHeader*>(ip_view->payload.data())
              ->dest_port) == kDhcpv4ClientPort;
  const bool is_local =
      iface->HasAddress(ip_view->dst) || ip_view->dst.IsBroadcast() ||
      ip_view->dst.IsMulticast() || ip_view->dst == ClatLocalIpv4Address() ||
      is_dhcpv4;
  if (!is_local) {
    ForwardingConfig fwd_cfg = GetForwardingConfig();
    if (!fwd_cfg.ipv4_forwarding_enabled) return;
    auto fwd_ifaces = BuildForwardingInterfaces();
    ForwardingResult fwd = EvaluatePacketForwarding(
        packet, iface_idx, fwd_cfg, GetRoutingTable(), fwd_ifaces, {}, now);
    if (fwd.action == ForwardingAction::Forward ||
        fwd.action == ForwardingAction::SendIcmpError) {
      (void)SendRawIpPacket(fwd.egress_interface_index, fwd.next_hop,
                            fwd.packet);
      return;
    }
    if (fwd.action != ForwardingAction::DeliverLocally) return;
  }

  std::string reassembled_buf;
  std::string_view upper_payload = ip_view->payload;
  uint8 upper_proto = ip_view->protocol;

  if (packet.size() >= sizeof(IpHeader)) {
    const auto* raw_hdr = reinterpret_cast<const IpHeader*>(packet.data());
    const uint16 frag_field = Swap16BitEndian(raw_hdr->flags_fragment);
    const bool more_frags = (frag_field & kIpv4MoreFragmentsMask) != 0;
    const uint16 frag_off_bytes =
        static_cast<uint16>((frag_field & kIpv4FragmentOffsetMask) * 8);
    if (more_frags || frag_off_bytes != 0) {
      ReassemblyKey key;
      key.family = IpAddressFamily::V4;
      key.source = ip_view->src;
      key.destination = ip_view->dst;
      key.identification = Swap16BitEndian(raw_hdr->identification);
      key.protocol = ip_view->protocol;
      ReassemblyResult r = GetReassembler().AddFragment(
          key, frag_off_bytes, more_frags, ip_view->payload, now);
      if (r.status != ReassemblyStatus::Complete) return;
      reassembled_buf = std::move(r.payload);
      upper_payload = reassembled_buf;
      upper_proto = r.protocol;
    }
  }

  FirewallPacket fw_pkt = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, ip_view->src, ip_view->dst, upper_proto,
      upper_payload, ip_view->hop_limit, static_cast<uint32>(iface_idx));
  if (!IsInboundPermittedByFirewall(fw_pkt, now)) return;

  IpPacketView effective_view = *ip_view;
  effective_view.protocol = upper_proto;
  effective_view.payload = upper_payload;

  if (upper_proto == kProtocolIcmpv4) {
    ProcessIcmpv4(effective_view, iface_idx);
  } else if (upper_proto == kProtocolUdp) {
    ProcessUdp(iface_idx, effective_view.src, effective_view.dst,
               effective_view.payload);
  } else if (upper_proto == kProtocolTcp) {
    ProcessTcpSegment(iface_idx, effective_view.src, effective_view.dst,
                      effective_view.payload);
  }
}

void ProcessIpv6(std::string_view frame_payload, size_t iface_idx) {
  const auto now = std::chrono::steady_clock::now();
  IpsecResult ipsec_res = GetIpsecEngine().ProcessInbound(frame_payload);
  if (ipsec_res.status == IpsecStatus::Dropped) return;
  std::string_view packet = (ipsec_res.status == IpsecStatus::Protected)
                                ? std::string_view(ipsec_res.packet)
                                : frame_payload;

  auto fixed = ParseIpv6Header(packet);
  if (!fixed.has_value()) return;
  std::string_view trimmed =
      packet.substr(0, kIpv6HeaderSize + fixed->payload_length);

  NetworkInterface* iface = GetInterface(iface_idx);
  if (iface == nullptr) return;

  if (!iface->HasAddress(fixed->destination) &&
      !fixed->destination.IsMulticast()) {
    ForwardingConfig fwd_cfg = GetForwardingConfig();
    if (!fwd_cfg.ipv6_forwarding_enabled) return;
    auto fwd_ifaces = BuildForwardingInterfaces();
    ForwardingResult fwd = EvaluatePacketForwarding(
        trimmed, iface_idx, fwd_cfg, GetRoutingTable(), fwd_ifaces, {}, now);
    if (fwd.action == ForwardingAction::Forward ||
        fwd.action == ForwardingAction::SendIcmpError) {
      (void)SendRawIpPacket(fwd.egress_interface_index, fwd.next_hop,
                            fwd.packet);
      return;
    }
    if (fwd.action != ForwardingAction::DeliverLocally) return;
  }

  // 464XLAT CLAT inbound translation for packets arriving from a NAT64 prefix.
  if (iface->nat64_prefix.has_value() && iface->nat64_prefix->IsValid() &&
      fixed->source.IsInPrefix(iface->nat64_prefix->prefix,
                               iface->nat64_prefix->prefix_length)) {
    auto v4_pkt =
        TranslateIpv6ToIpv4(trimmed, fixed->destination, *iface->nat64_prefix);
    if (v4_pkt.has_value()) {
      ProcessIpv4(*v4_pkt, iface_idx);
      return;
    }
  }

  ExtensionWalkResult walk = WalkExtensionHeaders(*fixed, trimmed);
  if (walk.status == ExtensionWalkStatus::Discard) return;
  if (walk.status == ExtensionWalkStatus::ParameterProblem) {
    if (CanSendIcmpv6Error(Icmpv6Type::ParameterProblem, walk.problem_code,
                           trimmed) &&
        GetIcmpv6RateLimiter().Allow(now)) {
      IpAddress err_src = fixed->destination.IsMulticast()
                              ? SelectSourceAddress(iface_idx, fixed->source)
                                    .value_or(LinkLocalAddressFromMac(iface->mac))
                              : fixed->destination;
      auto err_icmp = BuildIcmpv6Error(
          err_src, fixed->source, Icmpv6Type::ParameterProblem,
          walk.problem_code, walk.problem_pointer, trimmed);
      if (err_icmp.has_value()) {
        IpPacketRequest req;
        req.interface_index = iface_idx;
        req.src = err_src;
        req.dst = fixed->source;
        req.protocol = kProtocolIcmpv6;
        req.payload = *err_icmp;
        (void)SendIpPacket(req);
      }
    }
    return;
  }
  if (walk.status == ExtensionWalkStatus::NoNextHeader) return;

  std::string reassembled_buf;
  std::string_view upper_payload = walk.payload;
  uint8 upper_proto = walk.next_header;

  if (walk.status == ExtensionWalkStatus::Fragment &&
      walk.fragment.has_value()) {
    ReassemblyKey key;
    key.family = IpAddressFamily::V6;
    key.source = fixed->source;
    key.destination = fixed->destination;
    key.identification = walk.fragment->identification;
    key.protocol = walk.next_header;
    ReassemblyResult r = GetReassembler().AddFragment(
        key, walk.fragment->offset, walk.fragment->more_fragments,
        walk.payload, now, walk.fragment_header_offset);
    if (r.status != ReassemblyStatus::Complete) return;
    reassembled_buf = std::move(r.payload);
    upper_payload = reassembled_buf;
    upper_proto = r.protocol;
  }

  if (upper_proto == static_cast<uint8>(Ipv6NextHeader::NoNextHeader)) return;

  FirewallPacket fw_pkt = FirewallPacket::FromPayload(
      FirewallDirection::Inbound, fixed->source, fixed->destination,
      upper_proto, upper_payload, fixed->hop_limit,
      static_cast<uint32>(iface_idx));
  if (!IsInboundPermittedByFirewall(fw_pkt, now)) return;

  if (upper_proto == kProtocolIcmpv6) {
    ProcessIcmpv6(fixed->source, fixed->destination, fixed->hop_limit,
                  walk.router_alert, upper_payload, iface_idx);
  } else if (upper_proto == kProtocolUdp) {
    ProcessUdp(iface_idx, fixed->source, fixed->destination, upper_payload);
  } else if (upper_proto == kProtocolTcp) {
    ProcessTcpSegment(iface_idx, fixed->source, fixed->destination,
                      upper_payload);
  }
}

}  // namespace

NetworkListener::NetworkListener(size_t interface_index)
    : ::perception::devices::NetworkListener::Server(),
      interface_index_(interface_index) {}

Status NetworkListener::PacketReceived(
    const ::perception::devices::Packet& packet) {
  if (packet.data.size() < kEthernetHeaderSize) return Status::OK;

  std::string data = packet.data;
  size_t iface_idx = interface_index_;
  ::perception::Defer([data = std::move(data), iface_idx]() {
    NetworkInterface* iface = GetInterface(iface_idx);
    if (iface == nullptr) return;

    auto eth_frame = ParseEthernetFrame(data);
    if (!eth_frame.has_value()) return;
    if (!iface->IsDestinedForMac(eth_frame->dest_mac)) return;

    if (eth_frame->ether_type == kEtherTypeArp) {
      ProcessArp(eth_frame->payload, iface_idx);
    } else if (eth_frame->ether_type == kEtherTypeIpv4) {
      ProcessIpv4(eth_frame->payload, iface_idx);
    } else if (eth_frame->ether_type == kEtherTypeIpv6) {
      ProcessIpv6(eth_frame->payload, iface_idx);
    }
  });
  return Status::OK;
}

void CreateAndAddNetworkListener(
    size_t interface_index,
    ::perception::devices::NetworkDevice::Client device) {
  auto listener = std::make_shared<NetworkListener>(interface_index);
  listeners.push_back(listener);
  auto status = device.SetPacketListener(
      ::perception::devices::NetworkListener::Client(*listener));
  if (status != Status::OK) {
    std::cout << "SetPacketListener failed! Status=" << static_cast<int>(status)
              << std::endl;
  }
}

void CreateAndAddNetworkListener(size_t interface_index) {
  CreateAndAddNetworkListener(interface_index,
                              GetNetworkInterface(interface_index).device);
}
