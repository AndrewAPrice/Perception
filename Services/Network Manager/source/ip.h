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

#include <types.h>

#include <string_view>

#include "ethernet.h"
#include "perception/network/ip_address.h"
#include "status.h"

// Parameters for transmitting an IPv4 or IPv6 packet via the unified IP TX
// path.
struct IpPacketRequest {
  // Index of the outgoing network interface.
  size_t interface_index = 0;
  // Source IP address; if Unspecified, selected from `interface_index` for
  // `dst.family()`.
  ::perception::network::IpAddress src;
  // Destination IP address.
  ::perception::network::IpAddress dst;
  // Upper-layer protocol number (e.g. 1 = ICMPv4, 6 = TCP, 17 = UDP,
  // 58 = ICMPv6).
  uint8 protocol = 0;
  // Upper-layer payload (already containing its transport/ICMP checksum).
  std::string_view payload;
  // Optional TTL / Hop Limit override (0 uses the interface default, 64).
  uint8 hop_limit = 0;
  // Whether to set the IPv4 Don't Fragment flag (true for TCP).
  bool dont_fragment = false;
  // Optional explicit next-hop override; if Unspecified, selected via
  // `iface->SelectNextHop(dst)`.
  ::perception::network::IpAddress next_hop_override;
  // Optional Hop-by-Hop Router Alert option for MLDv2 reports.
  bool include_ipv6_router_alert = false;
};

// Transmits an IP packet on the specified interface, performing source address
// selection (if `req.src` is unspecified), outbound firewall + IPsec + CLAT
// processing, Path MTU fragmentation, next-hop selection, neighbor resolution,
// and Ethernet frame transmission.
Status SendIpPacket(const IpPacketRequest& req);

// Transmits an already-serialized IPv4 or IPv6 packet on `interface_index` to
// `next_hop` (or selects the next hop from the packet's destination if
// `next_hop` is Unspecified).
Status SendRawIpPacket(size_t interface_index,
                       const ::perception::network::IpAddress& next_hop,
                       std::string_view raw_ip_packet);
