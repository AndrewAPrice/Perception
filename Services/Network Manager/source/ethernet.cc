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

#include "ethernet.h"

#include <algorithm>
#include <cstring>

#include "checksum.h"
#include "wire_format.h"

namespace {

using ::perception::network::IpAddress;
using ::perception::network::IpAddressFamily;

// IPv4 version nibble.
constexpr uint8 kIpv4Version = 4;

// Minimum IPv4 header length in bytes.
constexpr size_t kMinIpv4HeaderSize = 20;

// Version 4 + IHL 5 byte (0x45).
constexpr uint8 kIpv4VersionIhl20 = 0x45;

// Don't Fragment flag in the IPv4 flags_fragment field.
constexpr uint16 kIpv4DontFragment = 0x4000;

// Byte offset of the header checksum within an IPv4 header.
constexpr size_t kIpv4ChecksumOffset = 10;

}  // namespace

std::optional<EthernetFrameView> ParseEthernetFrame(std::string_view frame) {
  if (frame.size() < kEthernetHeaderSize) return std::nullopt;

  EthernetFrameView view;
  std::memcpy(view.dest_mac.data(), frame.data(), 6);
  std::memcpy(view.src_mac.data(), frame.data() + 6, 6);
  view.ether_type =
      (static_cast<uint16>(static_cast<uint8>(frame[12])) << 8) |
      static_cast<uint16>(static_cast<uint8>(frame[13]));
  view.payload = frame.substr(kEthernetHeaderSize);
  return view;
}

std::string BuildEthernetFrame(const std::array<uint8, 6>& src_mac,
                               const std::array<uint8, 6>& dest_mac,
                               uint16 ether_type, std::string_view payload) {
  size_t raw_size = kEthernetHeaderSize + payload.size();
  size_t frame_size = std::max(raw_size, kMinEthernetFrameSize);

  std::string out;
  out.reserve(frame_size);
  WireWriter writer(out);
  writer.WriteBytes(
      std::string_view(reinterpret_cast<const char*>(dest_mac.data()), 6));
  writer.WriteBytes(
      std::string_view(reinterpret_cast<const char*>(src_mac.data()), 6));
  writer.WriteU16(ether_type);
  writer.WriteBytes(payload);
  if (out.size() < frame_size) writer.WriteZeros(frame_size - out.size());
  return out;
}

std::optional<std::array<uint8, 6>> MulticastMac(const IpAddress& group) {
  if (!group.IsMulticast()) return std::nullopt;
  const auto& b = group.bytes();
  if (group.IsV4()) {
    return std::array<uint8, 6>{0x01, 0x00, 0x5E,
                                static_cast<uint8>(b[1] & 0x7F), b[2], b[3]};
  }
  if (group.IsV6()) {
    return std::array<uint8, 6>{0x33, 0x33, b[12], b[13], b[14], b[15]};
  }
  return std::nullopt;
}

bool IsBroadcastMac(const std::array<uint8, 6>& mac) {
  return mac == kBroadcastMac;
}

bool IsMulticastMac(const std::array<uint8, 6>& mac) {
  return (mac[0] & 0x01) != 0;
}

std::optional<IpPacketView> ParseIpv4Packet(std::string_view frame_payload) {
  if (frame_payload.size() < kMinIpv4HeaderSize) return std::nullopt;

  uint8 version_ihl = static_cast<uint8>(frame_payload[0]);
  if ((version_ihl >> 4) != kIpv4Version) return std::nullopt;

  size_t ihl = static_cast<size_t>(version_ihl & 0x0F) * 4;
  if (ihl < kMinIpv4HeaderSize || frame_payload.size() < ihl)
    return std::nullopt;

  WireReader reader(frame_payload);
  reader.Skip(2);
  uint16 total_length = reader.ReadU16();
  if (total_length < ihl || total_length > frame_payload.size())
    return std::nullopt;

  if (InternetChecksum(frame_payload.substr(0, ihl)) != 0) return std::nullopt;

  reader.Skip(4);
  uint8 ttl = reader.ReadU8();
  uint8 protocol = reader.ReadU8();
  reader.Skip(2);
  std::string_view src_bytes = reader.ReadBytes(IpAddress::kV4Length);
  std::string_view dst_bytes = reader.ReadBytes(IpAddress::kV4Length);
  if (!reader.ok()) return std::nullopt;

  IpPacketView view;
  view.family = IpAddressFamily::V4;
  view.src = IpAddress::FromBytes(
      IpAddressFamily::V4,
      {reinterpret_cast<const uint8*>(src_bytes.data()), src_bytes.size()});
  view.dst = IpAddress::FromBytes(
      IpAddressFamily::V4,
      {reinterpret_cast<const uint8*>(dst_bytes.data()), dst_bytes.size()});
  view.protocol = protocol;
  view.hop_limit = ttl;
  view.raw_ip_packet = frame_payload.substr(0, total_length);
  view.payload = frame_payload.substr(ihl, total_length - ihl);
  return view;
}

std::string BuildIpv4Packet(const IpAddress& src, const IpAddress& dst,
                            uint8 protocol, std::string_view payload, uint8 ttl,
                            bool dont_fragment) {
  uint16 total_length = static_cast<uint16>(kMinIpv4HeaderSize + payload.size());
  std::string out;
  out.reserve(total_length);
  WireWriter writer(out);
  writer.WriteU8(kIpv4VersionIhl20);
  writer.WriteU8(0);
  writer.WriteU16(total_length);
  writer.WriteU16(0);
  writer.WriteU16(dont_fragment ? kIpv4DontFragment : 0);
  writer.WriteU8(ttl);
  writer.WriteU8(protocol);
  writer.WriteU16(0);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    writer.WriteU8(src.bytes()[i]);
  for (size_t i = 0; i < IpAddress::kV4Length; i++)
    writer.WriteU8(dst.bytes()[i]);

  uint16 header_checksum =
      InternetChecksum(std::string_view(out).substr(0, kMinIpv4HeaderSize));
  writer.PatchU16(kIpv4ChecksumOffset, header_checksum);
  writer.WriteBytes(payload);
  return out;
}
