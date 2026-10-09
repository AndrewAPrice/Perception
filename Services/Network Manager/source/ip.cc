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

#include "ip.h"

#include <chrono>
#include <string>

#include "firewall.h"
#include "fragmentation.h"
#include "interface.h"
#include "ipsec.h"
#include "ipv6_header.h"
#include "nat64_clat.h"

namespace {

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

// Default IPv4 Time-To-Live when none is specified in IpPacketRequest.
constexpr uint8 kDefaultIpv4Ttl = 64;

}  // namespace

Status SendRawIpPacket(size_t interface_index, const IpAddress& next_hop,
                       std::string_view raw_ip_packet) {
  if (raw_ip_packet.empty()) return Status::INVALID_ARGUMENT;
  NetworkInterface* iface = GetInterface(interface_index);
  if (iface == nullptr) return Status::MISSING_MEDIA;

  const auto now = std::chrono::steady_clock::now();
  const uint8 version = static_cast<uint8>(raw_ip_packet[0]) >> 4;
  uint16 ether_type = 0;
  IpAddress dst;
  if (version == 4) {
    auto view = ParseIpv4Packet(raw_ip_packet);
    if (!view.has_value()) return Status::INVALID_ARGUMENT;
    ether_type = kEtherTypeIpv4;
    dst = view->dst;
    FirewallPacket fw_pkt = FirewallPacket::FromPayload(
        FirewallDirection::Outbound, view->src, view->dst, view->protocol,
        view->payload, view->hop_limit, static_cast<uint32>(interface_index));
    GetFirewall().RecordOutboundPacket(fw_pkt, now);
  } else if (version == 6) {
    auto hdr = ParseIpv6Header(raw_ip_packet);
    if (!hdr.has_value()) return Status::INVALID_ARGUMENT;
    ether_type = kEtherTypeIpv6;
    dst = hdr->destination;
    std::string_view trimmed =
        raw_ip_packet.substr(0, kIpv6HeaderSize + hdr->payload_length);
    ExtensionWalkResult walk = WalkExtensionHeaders(*hdr, trimmed);
    if (walk.status == ExtensionWalkStatus::UpperLayer) {
      FirewallPacket fw_pkt = FirewallPacket::FromPayload(
          FirewallDirection::Outbound, hdr->source, hdr->destination,
          walk.next_header, walk.payload, hdr->hop_limit,
          static_cast<uint32>(interface_index));
      GetFirewall().RecordOutboundPacket(fw_pkt, now);
    }
  } else {
    return Status::INVALID_ARGUMENT;
  }

  IpAddress resolved_next_hop = next_hop;
  if (resolved_next_hop.IsUnspecified()) {
    auto selected = SelectNextHop(interface_index, dst);
    if (!selected.has_value()) return Status::MISSING_MEDIA;
    resolved_next_hop = *selected;
  }

  if (!ResolveAndSendFrame(interface_index, resolved_next_hop, ether_type,
                           raw_ip_packet)) {
    return Status::INTERNAL_ERROR;
  }
  return Status::OK;
}

Status SendIpPacket(const IpPacketRequest& req) {
  NetworkInterface* iface = GetInterface(req.interface_index);
  if (iface == nullptr || req.dst.IsUnspecified())
    return Status::INVALID_ARGUMENT;

  const auto now = std::chrono::steady_clock::now();
  IpAddress src = req.src;
  if (src.IsUnspecified()) {
    auto selected = SelectSourceAddress(req.interface_index, req.dst);
    if (!selected.has_value()) {
      // 464XLAT CLAT fallback for IPv4 destinations on an IPv6-only network.
      if (req.dst.IsV4() && iface->nat64_prefix.has_value() &&
          iface->nat64_prefix->IsValid()) {
        auto v6_src = iface->GetPreferredAddress(IpAddressFamily::V6);
        if (v6_src.has_value()) {
          uint8 ttl = req.hop_limit != 0 ? req.hop_limit : kDefaultIpv4Ttl;
          FirewallPacket clat_fw_pkt = FirewallPacket::FromPayload(
              FirewallDirection::Outbound, ClatLocalIpv4Address(), req.dst,
              req.protocol, req.payload, ttl,
              static_cast<uint32>(req.interface_index));
          if (GetFirewall().Evaluate(clat_fw_pkt, now) != FirewallAction::Allow)
            return Status::NOT_ALLOWED;
          GetFirewall().RecordOutboundPacket(clat_fw_pkt, now);
          std::string v4_pkt =
              BuildIpv4Packet(ClatLocalIpv4Address(), req.dst, req.protocol,
                              req.payload, ttl, req.dont_fragment);
          auto translated =
              TranslateIpv4ToIpv6(v4_pkt, *v6_src, *iface->nat64_prefix);
          if (translated.has_value())
            return SendRawIpPacket(req.interface_index, IpAddress{},
                                   *translated);
        }
      }
      return Status::MISSING_MEDIA;
    }
    src = *selected;
  }

  const uint8 effective_hop_limit =
      req.hop_limit != 0
          ? req.hop_limit
          : (req.dst.IsV6() ? iface->ipv6_hop_limit : kDefaultIpv4Ttl);

  FirewallPacket fw_pkt = FirewallPacket::FromPayload(
      FirewallDirection::Outbound, src, req.dst, req.protocol, req.payload,
      effective_hop_limit, static_cast<uint32>(req.interface_index));
  if (GetFirewall().Evaluate(fw_pkt, now) != FirewallAction::Allow)
    return Status::NOT_ALLOWED;
  GetFirewall().RecordOutboundPacket(fw_pkt, now);

  IpAddress next_hop = req.next_hop_override;
  if (next_hop.IsUnspecified()) {
    auto selected = SelectNextHop(req.interface_index, req.dst);
    if (!selected.has_value()) return Status::MISSING_MEDIA;
    next_hop = *selected;
  }

  const uint16 path_mtu = GetEffectivePathMtu(req.dst);

  if (req.dst.IsV4()) {
    Ipv4Datagram dg;
    dg.source = src;
    dg.destination = req.dst;
    dg.protocol = req.protocol;
    dg.ttl = effective_hop_limit;
    dg.dont_fragment = req.dont_fragment;
    dg.payload.assign(req.payload.data(), req.payload.size());

    auto fragments = FragmentIpv4Datagram(
        dg, path_mtu, GetFragmentIdGenerator().NextIpv4Id(req.dst));
    if (!fragments.has_value()) return Status::INVALID_ARGUMENT;

    for (const std::string& frag : *fragments) {
      IpsecResult ipsec_res = GetIpsecEngine().ProcessOutbound(frag);
      if (ipsec_res.status == IpsecStatus::Dropped) return Status::NOT_ALLOWED;
      const std::string& out_pkt =
          (ipsec_res.status == IpsecStatus::Protected) ? ipsec_res.packet : frag;
      if (!ResolveAndSendFrame(req.interface_index, next_hop, kEtherTypeIpv4,
                               out_pkt)) {
        return Status::INTERNAL_ERROR;
      }
    }
    return Status::OK;
  }

  if (req.dst.IsV6()) {
    Ipv6Datagram dg;
    dg.source = src;
    dg.destination = req.dst;
    dg.next_header = req.protocol;
    dg.hop_limit = effective_hop_limit;
    dg.router_alert = req.include_ipv6_router_alert;
    dg.payload.assign(req.payload.data(), req.payload.size());

    auto fragments = FragmentIpv6Datagram(
        dg, path_mtu, GetFragmentIdGenerator().NextIpv6Id(req.dst));
    if (!fragments.has_value()) return Status::INVALID_ARGUMENT;

    for (const std::string& frag : *fragments) {
      IpsecResult ipsec_res = GetIpsecEngine().ProcessOutbound(frag);
      if (ipsec_res.status == IpsecStatus::Dropped) return Status::NOT_ALLOWED;
      const std::string& out_pkt =
          (ipsec_res.status == IpsecStatus::Protected) ? ipsec_res.packet : frag;
      if (!ResolveAndSendFrame(req.interface_index, next_hop, kEtherTypeIpv6,
                               out_pkt)) {
        return Status::INTERNAL_ERROR;
      }
    }
    return Status::OK;
  }

  return Status::INVALID_ARGUMENT;
}
