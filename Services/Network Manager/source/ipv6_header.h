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
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "perception/network/ip_address.h"

// Size of the fixed IPv6 header (RFC 8200 §3).
inline constexpr size_t kIpv6HeaderSize = 40;

// Smallest link MTU every IPv6 link must support (RFC 8200 §5).
inline constexpr size_t kIpv6MinimumMtu = 1280;

// Hop limit required on every Neighbor Discovery message (RFC 4861).
inline constexpr uint8 kNdpHopLimit = 255;

// Hop limit used for MLD messages (RFC 3810 §5).
inline constexpr uint8 kMldHopLimit = 1;

// Next Header values understood by the IPv6 layer.
enum class Ipv6NextHeader : uint8 {
  HopByHop = 0,
  Tcp = 6,
  Udp = 17,
  Routing = 43,
  Fragment = 44,
  Icmpv6 = 58,
  NoNextHeader = 59,
  DestinationOptions = 60,
};

// The fixed IPv6 header.
struct Ipv6Header {
  // DSCP and ECN bits.
  uint8 traffic_class = 0;
  // 20-bit flow label.
  uint32 flow_label = 0;
  // Length of everything after the fixed header.
  uint16 payload_length = 0;
  // Type of the header that follows the fixed header.
  uint8 next_header = 0;
  // Remaining hop count.
  uint8 hop_limit = 0;
  // Source address.
  ::perception::network::IpAddress source;
  // Destination address.
  ::perception::network::IpAddress destination;
};

// Parses the fixed header. Returns nullopt unless the version is 6 and the
// buffer holds at least kIpv6HeaderSize + payload_length bytes. Bytes beyond
// that (e.g. Ethernet padding) are ignored by later processing.
std::optional<Ipv6Header> ParseIpv6Header(std::string_view packet);

// Appends the 40-byte wire form of `header` to `out`.
void AppendIpv6Header(const Ipv6Header& header, std::string& out);

// An outgoing IPv6 datagram produced by a protocol component. The payload is
// the complete upper-layer message (checksum already filled in).
struct Ipv6Datagram {
  // Source address (may be unspecified for DAD and early MLD reports).
  ::perception::network::IpAddress source;
  // Destination address.
  ::perception::network::IpAddress destination;
  // Upper-layer protocol of `payload`.
  uint8 next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  // Hop limit to send with.
  uint8 hop_limit = 64;
  // Inserts a Hop-by-Hop header carrying a Router Alert (MLD) when true.
  bool router_alert = false;
  // Upper-layer message.
  std::string payload;
};

// Receives datagrams that a protocol component wants transmitted.
using Ipv6Sink = std::function<void(Ipv6Datagram)>;

// Serializes a datagram: fixed header, optional Hop-by-Hop Router Alert
// header, then the payload.
std::string SerializeIpv6Datagram(const Ipv6Datagram& datagram);

// Fields of a Fragment extension header.
struct Ipv6FragmentHeader {
  // Datagram identification.
  uint32 identification = 0;
  // Offset of this fragment's data in bytes.
  uint16 offset = 0;
  // True if more fragments follow.
  bool more_fragments = false;
};

// Outcome of walking the extension header chain.
enum class ExtensionWalkStatus {
  // `next_header`/`payload` describe a supported upper-layer protocol.
  UpperLayer,
  // The chain ended with No Next Header; nothing to deliver.
  NoNextHeader,
  // A non-atomic fragment; `payload` is fragment data for reassembly.
  Fragment,
  // Discard and reply with ICMPv6 Parameter Problem (`problem_*`).
  ParameterProblem,
  // Discard silently.
  Discard,
};

// Result of WalkExtensionHeaders.
struct ExtensionWalkResult {
  // What to do with the packet.
  ExtensionWalkStatus status = ExtensionWalkStatus::Discard;
  // Upper-layer protocol, or the protocol following a Fragment header.
  uint8 next_header = 0;
  // Offset of `payload` from the start of the IPv6 header.
  size_t payload_offset = 0;
  // Upper-layer data, or fragment data.
  std::string_view payload;
  // True if a Hop-by-Hop Router Alert option was present.
  bool router_alert = false;
  // Value of the Router Alert option (0 = MLD).
  uint16 router_alert_value = 0;
  // The Fragment header, if one was present (including atomic fragments).
  std::optional<Ipv6FragmentHeader> fragment;
  // Offset of the Fragment header from the start of the IPv6 header.
  size_t fragment_header_offset = 0;
  // ICMPv6 Parameter Problem code (0 erroneous field, 1 unknown next header,
  // 2 unrecognized option).
  uint8 problem_code = 0;
  // ICMPv6 Parameter Problem pointer (offset into the invoking packet).
  uint32 problem_pointer = 0;
};

// Returns true for upper-layer protocols the stack delivers (TCP, UDP,
// ICMPv6).
bool IsSupportedUpperLayerProtocol(uint8 next_header);

// Walks the extension headers of `packet` (which starts at the fixed header
// already validated by ParseIpv6Header) per RFC 8200 §4.
ExtensionWalkResult WalkExtensionHeaders(const Ipv6Header& header,
                                         std::string_view packet);

// Returns ff02::1 (all nodes).
::perception::network::IpAddress AllNodesMulticastAddress();

// Returns ff02::2 (all routers).
::perception::network::IpAddress AllRoutersMulticastAddress();

// Returns ff02::16 (all MLDv2-capable routers).
::perception::network::IpAddress AllMldv2RoutersMulticastAddress();

// Returns ff02::1:2 (all DHCP relay agents and servers).
::perception::network::IpAddress AllDhcpRelayAgentsAndServersAddress();

// Returns the solicited-node multicast group ff02::1:ffXX:XXXX of `address`.
::perception::network::IpAddress SolicitedNodeMulticastAddress(
    const ::perception::network::IpAddress& address);

// Returns the multicast scope (low nibble of byte 1) of an IPv6 multicast
// address.
uint8 MulticastScope(const ::perception::network::IpAddress& address);

// Returns the address made from the high 64 bits of `prefix` followed by the
// 64-bit `interface_identifier`.
::perception::network::IpAddress CombinePrefixAndInterfaceIdentifier(
    const ::perception::network::IpAddress& prefix,
    const std::array<uint8, 8>& interface_identifier);

// Returns true if the first `prefix_length` bits of `a` and `b` match.
bool PrefixMatches(const ::perception::network::IpAddress& a,
                   const ::perception::network::IpAddress& b,
                   uint8 prefix_length);
