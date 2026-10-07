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

#include "icmpv6.h"

#include <algorithm>

#include "checksum.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Size of an ICMPv6 header (type, code, checksum, 32-bit body header).
constexpr size_t kIcmpv6HeaderSize = 8;

// Offset of the 16-bit checksum field inside an ICMPv6 message.
constexpr size_t kChecksumOffset = 2;

// First informational ICMPv6 type value (types below this are errors).
constexpr uint8 kFirstInformationalType = 128;

// Maximum bytes of the invoking packet that can be quoted inside an ICMPv6
// error without exceeding the 1280-byte minimum IPv6 MTU.
constexpr size_t kMaxQuotedPacketSize =
    kIpv6MinimumMtu - kIpv6HeaderSize - kIcmpv6HeaderSize;

// Returns true if the invoking packet's upper-layer header is an ICMPv6 error
// or Redirect (RFC 4443 §2.4(e.1) and (e.2)).
bool InvokingPacketIsIcmpv6ErrorOrRedirect(const Ipv6Header& header,
                                           std::string_view packet) {
  // Walk known extension headers without failing if the invoking packet itself
  // had an extension header problem; inspect whatever header is reached.
  size_t limit = std::min(packet.size(),
                          kIpv6HeaderSize + static_cast<size_t>(header.payload_length));
  uint8 next = header.next_header;
  size_t offset = kIpv6HeaderSize;
  while (offset < limit) {
    if (next == static_cast<uint8>(Ipv6NextHeader::Icmpv6)) {
      if (offset + 1 > limit) return false;
      uint8 icmp_type = static_cast<uint8>(packet[offset]);
      return IsIcmpv6ErrorMessage(icmp_type) ||
             icmp_type == static_cast<uint8>(Icmpv6Type::Redirect);
    }
    if (next == static_cast<uint8>(Ipv6NextHeader::HopByHop) ||
        next == static_cast<uint8>(Ipv6NextHeader::Routing) ||
        next == static_cast<uint8>(Ipv6NextHeader::DestinationOptions)) {
      if (offset + 2 > limit) return false;
      next = static_cast<uint8>(packet[offset]);
      size_t hdr_len = (static_cast<uint8>(packet[offset + 1]) + 1) * 8;
      offset += hdr_len;
    } else if (next == static_cast<uint8>(Ipv6NextHeader::Fragment)) {
      if (offset + 8 > limit) return false;
      uint16 frag_off =
          (static_cast<uint16>(static_cast<uint8>(packet[offset + 2])) << 8) |
          static_cast<uint8>(packet[offset + 3]);
      if ((frag_off & 0xFFF8) != 0) return false;
      next = static_cast<uint8>(packet[offset]);
      offset += 8;
    } else {
      return false;
    }
  }
  return false;
}

}  // namespace

bool IsIcmpv6ErrorMessage(uint8 type) {
  return type < kFirstInformationalType;
}

bool VerifyIcmpv6Checksum(const IpAddress& source, const IpAddress& destination,
                          std::string_view icmpv6_packet) {
  if (icmpv6_packet.size() < kIcmpv6HeaderSize) return false;
  uint16 sum = TransportChecksum(
      source, destination, static_cast<uint8>(Ipv6NextHeader::Icmpv6),
      icmpv6_packet);
  return sum == 0;
}

void FinalizeIcmpv6Checksum(const IpAddress& source,
                            const IpAddress& destination,
                            std::string& icmpv6_packet) {
  if (icmpv6_packet.size() < kChecksumOffset + 2) return;
  WireWriter writer(icmpv6_packet);
  writer.PatchU16(kChecksumOffset, 0);
  uint16 checksum = TransportChecksum(
      source, destination, static_cast<uint8>(Ipv6NextHeader::Icmpv6),
      icmpv6_packet);
  writer.PatchU16(kChecksumOffset, checksum);
}

std::optional<Icmpv6EchoMessage> ParseIcmpv6Echo(
    std::string_view icmpv6_packet) {
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(2);
  uint16 identifier = reader.ReadU16();
  uint16 sequence = reader.ReadU16();
  if (!reader.ok() || code != 0) return std::nullopt;
  if (type != static_cast<uint8>(Icmpv6Type::EchoRequest) &&
      type != static_cast<uint8>(Icmpv6Type::EchoReply))
    return std::nullopt;

  Icmpv6EchoMessage msg;
  msg.is_reply = (type == static_cast<uint8>(Icmpv6Type::EchoReply));
  msg.identifier = identifier;
  msg.sequence = sequence;
  msg.data = std::string(reader.Rest());
  return msg;
}

std::string BuildIcmpv6Echo(const IpAddress& source,
                            const IpAddress& destination,
                            const Icmpv6EchoMessage& message) {
  std::string out;
  out.reserve(kIcmpv6HeaderSize + message.data.size());
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(message.is_reply ? Icmpv6Type::EchoReply
                                                     : Icmpv6Type::EchoRequest));
  writer.WriteU8(0);
  writer.WriteU16(0);
  writer.WriteU16(message.identifier);
  writer.WriteU16(message.sequence);
  writer.WriteBytes(message.data);
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

Ipv6Datagram MakeIcmpv6EchoReply(const IpAddress& local_source,
                                 const IpAddress& remote_destination,
                                 uint8 hop_limit,
                                 const Icmpv6EchoMessage& request) {
  Icmpv6EchoMessage reply = request;
  reply.is_reply = true;
  Ipv6Datagram datagram;
  datagram.source = local_source;
  datagram.destination = remote_destination;
  datagram.next_header = static_cast<uint8>(Ipv6NextHeader::Icmpv6);
  datagram.hop_limit = hop_limit;
  datagram.payload = BuildIcmpv6Echo(local_source, remote_destination, reply);
  return datagram;
}

std::optional<Icmpv6ErrorMessage> ParseIcmpv6Error(
    std::string_view icmpv6_packet) {
  WireReader reader(icmpv6_packet);
  uint8 type = reader.ReadU8();
  uint8 code = reader.ReadU8();
  reader.Skip(2);
  uint32 parameter = reader.ReadU32();
  if (!reader.ok()) return std::nullopt;
  if (type < static_cast<uint8>(Icmpv6Type::DestinationUnreachable) ||
      type > static_cast<uint8>(Icmpv6Type::ParameterProblem))
    return std::nullopt;

  Icmpv6ErrorMessage error;
  error.type = static_cast<Icmpv6Type>(type);
  error.code = code;
  error.parameter = parameter;
  error.invoking_packet = reader.Rest();
  if (error.invoking_packet.size() >= kIpv6HeaderSize) {
    WireReader inv_reader(error.invoking_packet);
    uint32 first_word = inv_reader.ReadU32();
    if ((first_word >> 28) == 6) {
      Ipv6Header hdr;
      hdr.traffic_class = (first_word >> 20) & 0xFF;
      hdr.flow_label = first_word & 0xFFFFF;
      hdr.payload_length = inv_reader.ReadU16();
      hdr.next_header = inv_reader.ReadU8();
      hdr.hop_limit = inv_reader.ReadU8();
      hdr.source = inv_reader.ReadIpv6Address();
      hdr.destination = inv_reader.ReadIpv6Address();
      if (inv_reader.ok()) error.invoking_header = hdr;
    }
  }
  return error;
}

bool CanSendIcmpv6Error(Icmpv6Type error_type, uint8 error_code,
                        std::string_view invoking_packet) {
  if (invoking_packet.size() < kIpv6HeaderSize) return false;
  WireReader reader(invoking_packet);
  uint32 first_word = reader.ReadU32();
  if ((first_word >> 28) != 6) return false;
  Ipv6Header hdr;
  hdr.traffic_class = (first_word >> 20) & 0xFF;
  hdr.flow_label = first_word & 0xFFFFF;
  hdr.payload_length = reader.ReadU16();
  hdr.next_header = reader.ReadU8();
  hdr.hop_limit = reader.ReadU8();
  hdr.source = reader.ReadIpv6Address();
  hdr.destination = reader.ReadIpv6Address();
  if (!reader.ok()) return false;

  // Source must uniquely identify a single node (RFC 4443 §2.4(e.6)).
  if (hdr.source.IsUnspecified() || hdr.source.IsMulticast()) return false;

  // Never reply to an ICMPv6 error or Redirect (RFC 4443 §2.4(e.1, e.2)).
  if (InvokingPacketIsIcmpv6ErrorOrRedirect(hdr, invoking_packet)) return false;

  // Multicast destinations are forbidden except for Packet Too Big and
  // Parameter Problem code 2 (RFC 4443 §2.4(e.3)).
  if (hdr.destination.IsMulticast()) {
    bool allowed_exception =
        (error_type == Icmpv6Type::PacketTooBig) ||
        (error_type == Icmpv6Type::ParameterProblem &&
         error_code ==
             static_cast<uint8>(Icmpv6ParameterProblemCode::UnrecognizedIpv6Option));
    if (!allowed_exception) return false;
  }
  return true;
}

std::optional<std::string> BuildIcmpv6Error(const IpAddress& source,
                                            const IpAddress& destination,
                                            Icmpv6Type type, uint8 code,
                                            uint32 parameter,
                                            std::string_view invoking_packet) {
  if (!CanSendIcmpv6Error(type, code, invoking_packet)) return std::nullopt;
  std::string_view quoted =
      invoking_packet.substr(0, std::min(invoking_packet.size(), kMaxQuotedPacketSize));

  std::string out;
  out.reserve(kIcmpv6HeaderSize + quoted.size());
  WireWriter writer(out);
  writer.WriteU8(static_cast<uint8>(type));
  writer.WriteU8(code);
  writer.WriteU16(0);
  writer.WriteU32(parameter);
  writer.WriteBytes(quoted);
  FinalizeIcmpv6Checksum(source, destination, out);
  return out;
}

Icmpv6RateLimiter::Icmpv6RateLimiter(double rate_per_second,
                                     double burst_tokens)
    : rate_per_second_(rate_per_second),
      burst_tokens_(burst_tokens),
      tokens_(burst_tokens) {}

bool Icmpv6RateLimiter::Allow(std::chrono::steady_clock::time_point now) {
  if (last_refill_.has_value()) {
    double elapsed =
        std::chrono::duration<double>(now - *last_refill_).count();
    if (elapsed > 0.0)
      tokens_ = std::min(burst_tokens_, tokens_ + elapsed * rate_per_second_);
  }
  last_refill_ = now;
  if (tokens_ < 1.0) return false;
  tokens_ -= 1.0;
  return true;
}
