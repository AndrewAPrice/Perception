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

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "perception/network/ip_address.h"

// EtherType for IPv4 (0x0800).
inline constexpr uint16 kEtherTypeIpv4 = 0x0800;

// EtherType for ARP (0x0806).
inline constexpr uint16 kEtherTypeArp = 0x0806;

// EtherType for IPv6 (0x86DD).
inline constexpr uint16 kEtherTypeIpv6 = 0x86DD;

// Size of an Ethernet II header in bytes (destination MAC, source MAC, type).
inline constexpr size_t kEthernetHeaderSize = 14;

// Minimum Ethernet frame size in bytes (excluding FCS), padded with zeros.
inline constexpr size_t kMinEthernetFrameSize = 60;

// Broadcast Ethernet MAC address (FF:FF:FF:FF:FF:FF).
inline constexpr std::array<uint8, 6> kBroadcastMac = {0xFF, 0xFF, 0xFF,
                                                       0xFF, 0xFF, 0xFF};

// Parsed view of an Ethernet II frame.
struct EthernetFrameView {
  // Destination hardware MAC address.
  std::array<uint8, 6> dest_mac{};
  // Source hardware MAC address.
  std::array<uint8, 6> src_mac{};
  // EtherType in host byte order.
  uint16 ether_type = 0;
  // Payload after the 14-byte Ethernet header.
  std::string_view payload;
};

// Parsed view over an inbound IPv4 or IPv6 packet.
struct IpPacketView {
  // Address family of the packet.
  ::perception::network::IpAddressFamily family =
      ::perception::network::IpAddressFamily::Unspecified;
  // Source IP address.
  ::perception::network::IpAddress src;
  // Destination IP address.
  ::perception::network::IpAddress dst;
  // Upper-layer protocol number (IPv4 protocol or IPv6 terminal next header).
  uint8 protocol = 0;
  // Remaining TTL or Hop Limit.
  uint8 hop_limit = 0;
  // Upper-layer payload view, clamped to the IP header length (excluding any
  // trailing Ethernet frame padding).
  std::string_view payload;
  // Full IP packet view (header + payload), clamped to total_length.
  std::string_view raw_ip_packet;
};

// Parses an Ethernet II frame, returning nullopt if shorter than 14 bytes.
std::optional<EthernetFrameView> ParseEthernetFrame(std::string_view frame);

// Builds an Ethernet II frame and pads it with trailing zeros to at least
// kMinEthernetFrameSize (60 bytes).
std::string BuildEthernetFrame(const std::array<uint8, 6>& src_mac,
                               const std::array<uint8, 6>& dest_mac,
                               uint16 ether_type, std::string_view payload);

// Maps an IPv4 or IPv6 multicast group address to its Ethernet multicast MAC
// (01:00:5E + low 23 bits for IPv4; 33:33 + low 32 bits for IPv6). Returns
// nullopt if `group` is not a multicast address.
std::optional<std::array<uint8, 6>> MulticastMac(
    const ::perception::network::IpAddress& group);

// Returns true if `mac` is the broadcast MAC address (FF:FF:FF:FF:FF:FF).
bool IsBroadcastMac(const std::array<uint8, 6>& mac);

// Returns true if `mac` has the multicast group bit set (least-significant bit
// of the first octet).
bool IsMulticastMac(const std::array<uint8, 6>& mac);

// Parses and validates an IPv4 packet from an Ethernet frame payload.
// Validates version == 4, IHL >= 20, total_length >= IHL,
// total_length <= frame_payload.size() (preventing trailing-padding overreads),
// and the RFC 1071 IPv4 header checksum.
std::optional<IpPacketView> ParseIpv4Packet(std::string_view frame_payload);

// Builds a 20-byte IPv4 header + `payload` with a computed header checksum.
std::string BuildIpv4Packet(const ::perception::network::IpAddress& src,
                            const ::perception::network::IpAddress& dst,
                            uint8 protocol, std::string_view payload,
                            uint8 ttl = 64, bool dont_fragment = false);
