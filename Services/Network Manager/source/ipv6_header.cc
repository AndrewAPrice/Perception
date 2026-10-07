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

#include "ipv6_header.h"

#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Value of the version field for IPv6.
constexpr uint8 kIpv6Version = 6;

// Offset of the Next Header field in the fixed header.
constexpr size_t kNextHeaderFieldOffset = 6;

// Offset of the Payload Length field in the fixed header.
constexpr size_t kPayloadLengthFieldOffset = 4;

// Size of a Fragment extension header.
constexpr size_t kFragmentHeaderSize = 8;

// Extension header lengths are expressed in units of this many bytes.
constexpr size_t kExtensionHeaderUnit = 8;

// Largest reassembled IPv6 payload without jumbograms.
constexpr size_t kMaxIpv6Payload = 65535;

// Pad1 option type.
constexpr uint8 kOptionPad1 = 0;

// PadN option type.
constexpr uint8 kOptionPadN = 1;

// Router Alert option type (RFC 2711).
constexpr uint8 kOptionRouterAlert = 5;

// Length of the Router Alert option data.
constexpr uint8 kRouterAlertDataLength = 2;

// Router Alert value meaning "MLD message" (RFC 2711).
constexpr uint16 kRouterAlertMld = 0;

// Parameter Problem code: erroneous header field.
constexpr uint8 kProblemErroneousField = 0;

// Parameter Problem code: unrecognized Next Header.
constexpr uint8 kProblemUnknownNextHeader = 1;

// Parameter Problem code: unrecognized IPv6 option.
constexpr uint8 kProblemUnknownOption = 2;

// Size of the Hop-by-Hop header emitted for Router Alert.
constexpr size_t kRouterAlertHeaderSize = 8;

// Returns the byte at `offset` of `packet`.
uint8 ByteAt(std::string_view packet, size_t offset) {
  return static_cast<uint8>(packet[offset]);
}

// Returns a result that requests a Parameter Problem.
ExtensionWalkResult Problem(ExtensionWalkResult result, uint8 code,
                            size_t pointer) {
  result.status = ExtensionWalkStatus::ParameterProblem;
  result.problem_code = code;
  result.problem_pointer = static_cast<uint32>(pointer);
  return result;
}

// Returns a result that silently discards the packet.
ExtensionWalkResult Discard(ExtensionWalkResult result) {
  result.status = ExtensionWalkStatus::Discard;
  return result;
}

// Processes the TLV options of a Hop-by-Hop or Destination Options header
// spanning [start, end) of `packet`. Returns nullopt to keep walking, or the
// final result if the packet must be dropped.
std::optional<ExtensionWalkResult> ProcessOptions(
    std::string_view packet, size_t start, size_t end, bool hop_by_hop,
    bool destination_is_multicast, ExtensionWalkResult& result) {
  size_t offset = start;
  while (offset < end) {
    uint8 type = ByteAt(packet, offset);
    if (type == kOptionPad1) {
      offset++;
      continue;
    }
    if (offset + 2 > end)
      return Problem(result, kProblemErroneousField, offset);
    uint8 length = ByteAt(packet, offset + 1);
    if (offset + 2 + length > end)
      return Problem(result, kProblemErroneousField, offset + 1);

    if (type == kOptionPadN) {
      // Padding carries no information.
    } else if (type == kOptionRouterAlert && hop_by_hop) {
      if (length != kRouterAlertDataLength)
        return Problem(result, kProblemErroneousField, offset + 1);
      result.router_alert = true;
      result.router_alert_value =
          (ByteAt(packet, offset + 2) << 8) | ByteAt(packet, offset + 3);
    } else {
      switch (type >> 6) {
        case 0:
          break;
        case 1:
          return Discard(result);
        case 2:
          return Problem(result, kProblemUnknownOption, offset);
        default:
          if (destination_is_multicast) return Discard(result);
          return Problem(result, kProblemUnknownOption, offset);
      }
    }
    offset += 2 + length;
  }
  return std::nullopt;
}

}  // namespace

std::optional<Ipv6Header> ParseIpv6Header(std::string_view packet) {
  WireReader reader(packet);
  uint32 first_word = reader.ReadU32();
  Ipv6Header header;
  header.traffic_class = (first_word >> 20) & 0xFF;
  header.flow_label = first_word & 0xFFFFF;
  header.payload_length = reader.ReadU16();
  header.next_header = reader.ReadU8();
  header.hop_limit = reader.ReadU8();
  header.source = reader.ReadIpv6Address();
  header.destination = reader.ReadIpv6Address();
  if (!reader.ok() || (first_word >> 28) != kIpv6Version) return std::nullopt;
  if (reader.Remaining() < header.payload_length) return std::nullopt;
  return header;
}

void AppendIpv6Header(const Ipv6Header& header, std::string& out) {
  WireWriter writer(out);
  writer.WriteU32((static_cast<uint32>(kIpv6Version) << 28) |
                  (static_cast<uint32>(header.traffic_class) << 20) |
                  (header.flow_label & 0xFFFFF));
  writer.WriteU16(header.payload_length);
  writer.WriteU8(header.next_header);
  writer.WriteU8(header.hop_limit);
  writer.WriteIpv6Address(header.source);
  writer.WriteIpv6Address(header.destination);
}

std::string SerializeIpv6Datagram(const Ipv6Datagram& datagram) {
  Ipv6Header header;
  header.hop_limit = datagram.hop_limit;
  header.source = datagram.source;
  header.destination = datagram.destination;
  header.next_header =
      datagram.router_alert ? static_cast<uint8>(Ipv6NextHeader::HopByHop)
                            : datagram.next_header;
  header.payload_length = static_cast<uint16>(
      datagram.payload.size() +
      (datagram.router_alert ? kRouterAlertHeaderSize : 0));

  std::string out;
  out.reserve(kIpv6HeaderSize + header.payload_length);
  AppendIpv6Header(header, out);
  if (datagram.router_alert) {
    WireWriter writer(out);
    writer.WriteU8(datagram.next_header);
    // Header length in 8-byte units, not counting the first 8 bytes.
    writer.WriteU8(0);
    writer.WriteU8(kOptionRouterAlert);
    writer.WriteU8(kRouterAlertDataLength);
    writer.WriteU16(kRouterAlertMld);
    // PadN with no data fills the header to 8 bytes.
    writer.WriteU8(kOptionPadN);
    writer.WriteU8(0);
  }
  out.append(datagram.payload);
  return out;
}

bool IsSupportedUpperLayerProtocol(uint8 next_header) {
  switch (static_cast<Ipv6NextHeader>(next_header)) {
    case Ipv6NextHeader::Tcp:
    case Ipv6NextHeader::Udp:
    case Ipv6NextHeader::Icmpv6:
      return true;
    default:
      return false;
  }
}

ExtensionWalkResult WalkExtensionHeaders(const Ipv6Header& header,
                                         std::string_view packet) {
  ExtensionWalkResult result;
  size_t limit = kIpv6HeaderSize + header.payload_length;
  if (packet.size() < limit) return Discard(result);
  packet = packet.substr(0, limit);

  bool destination_is_multicast = header.destination.IsMulticast();
  uint8 next_header = header.next_header;
  size_t next_header_field = kNextHeaderFieldOffset;
  size_t offset = kIpv6HeaderSize;
  bool first = true;

  while (true) {
    switch (static_cast<Ipv6NextHeader>(next_header)) {
      case Ipv6NextHeader::HopByHop:
      case Ipv6NextHeader::DestinationOptions: {
        bool hop_by_hop = next_header == 0;
        if (hop_by_hop && !first)
          return Problem(result, kProblemUnknownNextHeader, next_header_field);
        if (offset + 2 > limit) return Discard(result);
        size_t length =
            (ByteAt(packet, offset + 1) + 1) * kExtensionHeaderUnit;
        if (offset + length > limit) return Discard(result);
        if (auto drop =
                ProcessOptions(packet, offset + 2, offset + length, hop_by_hop,
                               destination_is_multicast, result))
          return *drop;
        next_header = ByteAt(packet, offset);
        next_header_field = offset;
        offset += length;
        break;
      }
      case Ipv6NextHeader::Routing: {
        if (offset + 4 > limit) return Discard(result);
        size_t length =
            (ByteAt(packet, offset + 1) + 1) * kExtensionHeaderUnit;
        if (offset + length > limit) return Discard(result);
        // No routing type is supported (RH0 is deprecated by RFC 5095), so a
        // header that still has segments to visit points at the type field.
        if (ByteAt(packet, offset + 3) != 0)
          return Problem(result, kProblemErroneousField, offset + 2);
        next_header = ByteAt(packet, offset);
        next_header_field = offset;
        offset += length;
        break;
      }
      case Ipv6NextHeader::Fragment: {
        if (offset + kFragmentHeaderSize > limit) return Discard(result);
        WireReader reader(packet.substr(offset, kFragmentHeaderSize));
        uint8 fragment_next_header = reader.ReadU8();
        reader.Skip(1);
        uint16 offset_and_flags = reader.ReadU16();
        Ipv6FragmentHeader fragment;
        fragment.offset = offset_and_flags & 0xFFF8;
        fragment.more_fragments = (offset_and_flags & 1) != 0;
        fragment.identification = reader.ReadU32();
        result.fragment = fragment;
        result.fragment_header_offset = offset;
        next_header_field = offset;
        offset += kFragmentHeaderSize;

        if (fragment.offset == 0 && !fragment.more_fragments) {
          // Atomic fragment (RFC 6946): process as a whole packet.
          next_header = fragment_next_header;
          break;
        }
        size_t data_length = limit - offset;
        if (fragment.more_fragments &&
            (data_length == 0 || data_length % kExtensionHeaderUnit != 0))
          return Problem(result, kProblemErroneousField,
                         kPayloadLengthFieldOffset);
        if (fragment.offset + data_length > kMaxIpv6Payload)
          return Problem(result, kProblemErroneousField,
                         result.fragment_header_offset + 2);
        result.status = ExtensionWalkStatus::Fragment;
        result.next_header = fragment_next_header;
        result.payload_offset = offset;
        result.payload = packet.substr(offset);
        return result;
      }
      case Ipv6NextHeader::NoNextHeader:
        result.status = ExtensionWalkStatus::NoNextHeader;
        result.next_header = next_header;
        result.payload_offset = offset;
        return result;
      default:
        if (!IsSupportedUpperLayerProtocol(next_header))
          return Problem(result, kProblemUnknownNextHeader, next_header_field);
        result.status = ExtensionWalkStatus::UpperLayer;
        result.next_header = next_header;
        result.payload_offset = offset;
        result.payload = packet.substr(offset);
        return result;
    }
    first = false;
  }
}

IpAddress AllNodesMulticastAddress() {
  return IpAddress::V6({0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1});
}

IpAddress AllRoutersMulticastAddress() {
  return IpAddress::V6({0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2});
}

IpAddress AllMldv2RoutersMulticastAddress() {
  return IpAddress::V6(
      {0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x16});
}

IpAddress AllDhcpRelayAgentsAndServersAddress() {
  return IpAddress::V6({0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2});
}

IpAddress SolicitedNodeMulticastAddress(const IpAddress& address) {
  const auto& bytes = address.bytes();
  return IpAddress::V6({0xff, 0x02, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0xff,
                        bytes[13], bytes[14], bytes[15]});
}

uint8 MulticastScope(const IpAddress& address) {
  return address.bytes()[1] & 0x0F;
}

IpAddress CombinePrefixAndInterfaceIdentifier(
    const IpAddress& prefix, const std::array<uint8, 8>& interface_identifier) {
  std::array<uint8, 16> bytes = prefix.bytes();
  for (size_t i = 0; i < interface_identifier.size(); i++)
    bytes[8 + i] = interface_identifier[i];
  return IpAddress::V6(bytes);
}

bool PrefixMatches(const IpAddress& a, const IpAddress& b,
                   uint8 prefix_length) {
  const auto& a_bytes = a.bytes();
  const auto& b_bytes = b.bytes();
  size_t full_bytes = prefix_length / 8;
  for (size_t i = 0; i < full_bytes && i < a_bytes.size(); i++) {
    if (a_bytes[i] != b_bytes[i]) return false;
  }
  uint8 remaining_bits = prefix_length % 8;
  if (remaining_bits == 0 || full_bytes >= a_bytes.size()) return true;
  uint8 mask = static_cast<uint8>(0xFF << (8 - remaining_bits));
  return (a_bytes[full_bytes] & mask) == (b_bytes[full_bytes] & mask);
}
