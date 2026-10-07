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

#include "fragmentation.h"

#include <algorithm>

#include "checksum.h"
#include "wire_format.h"

using ::perception::network::IpAddress;

namespace {

// Fragment data in non-final fragments must be a multiple of 8 bytes.
constexpr size_t kFragmentAlignment = 8;

// Maximum payload size that can be represented by a 16-bit offset/length.
constexpr size_t kMaxPayloadSize = 65535;

// Size of the IPv6 Fragment extension header in bytes.
constexpr size_t kIpv6FragmentHeaderSize = 8;

// Size of the Hop-by-Hop Router Alert extension header in bytes.
constexpr size_t kRouterAlertHeaderSize = 8;

// Router Alert option type (RFC 2711).
constexpr uint8 kOptionRouterAlert = 5;

// Router Alert option length in bytes.
constexpr uint8 kRouterAlertDataLength = 2;

// Router Alert value for MLD (RFC 2711).
constexpr uint16 kRouterAlertMld = 0;

// PadN option type.
constexpr uint8 kOptionPadN = 1;

// Size of the fixed IPv4 header without options.
constexpr size_t kIpv4HeaderSize = 20;

// IPv4 version (4) and IHL (5 words = 20 bytes) byte.
constexpr uint8 kIpv4VersionAndIhl = 0x45;

// IPv4 Don't Fragment (DF) flag in the flags_offset field.
constexpr uint16 kIpv4DontFragmentFlag = 0x4000;

// IPv4 More Fragments (MF) flag in the flags_offset field.
constexpr uint16 kIpv4MoreFragmentsFlag = 0x2000;

// Offset of the header checksum within the 20-byte IPv4 header.
constexpr size_t kIpv4ChecksumOffset = 10;

// Maximum number of destinations tracked by FragmentIdGenerator.
constexpr size_t kMaxTrackedDestinations = 256;

// Initial non-zero state for the fallback xorshift32 PRNG.
constexpr uint32 kDefaultPrngSeed = 0x9E3779B9u;

// Default xorshift32 generator used when no external random source is passed.
FragmentIdGenerator::RandomSource DefaultRandomSource() {
  uint32 state = kDefaultPrngSeed;
  return [state]() mutable -> uint32 {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
  };
}

// Appends the 20-byte IPv4 header with a computed RFC 1071 checksum and
// `slice_data` to `out`.
std::string BuildIpv4Packet(const Ipv4Datagram& datagram, uint16 identification,
                            uint16 flags_and_offset,
                            std::string_view slice_data) {
  std::string packet;
  uint16 total_length =
      static_cast<uint16>(kIpv4HeaderSize + slice_data.size());
  packet.reserve(total_length);

  WireWriter writer(packet);
  writer.WriteU8(kIpv4VersionAndIhl);
  writer.WriteU8(datagram.dscp_ecn);
  writer.WriteU16(total_length);
  writer.WriteU16(identification);
  writer.WriteU16(flags_and_offset);
  writer.WriteU8(datagram.ttl);
  writer.WriteU8(datagram.protocol);
  writer.WriteU16(0);  // Checksum placeholder.
  const auto& src = datagram.source.bytes();
  const auto& dst = datagram.destination.bytes();
  for (size_t i = 0; i < IpAddress::kV4Length; i++) writer.WriteU8(src[i]);
  for (size_t i = 0; i < IpAddress::kV4Length; i++) writer.WriteU8(dst[i]);

  uint16 checksum = InternetChecksum(
      std::string_view(packet.data(), kIpv4HeaderSize));
  writer.PatchU16(kIpv4ChecksumOffset, checksum);
  writer.WriteBytes(slice_data);
  return packet;
}

}  // namespace

std::optional<std::vector<FragmentSlice>> PlanFragments(
    std::string_view payload, size_t max_fragment_payload) {
  if (payload.size() > kMaxPayloadSize) return std::nullopt;
  if (payload.size() <= max_fragment_payload)
    return std::vector<FragmentSlice>{{0, false, payload}};

  size_t aligned_chunk =
      (max_fragment_payload / kFragmentAlignment) * kFragmentAlignment;
  if (aligned_chunk < kFragmentAlignment) return std::nullopt;

  std::vector<FragmentSlice> slices;
  size_t offset = 0;
  while (offset < payload.size()) {
    size_t remaining = payload.size() - offset;
    bool more = remaining > max_fragment_payload;
    size_t chunk_size = more ? aligned_chunk : remaining;
    slices.push_back({static_cast<uint16>(offset), more,
                      payload.substr(offset, chunk_size)});
    offset += chunk_size;
  }
  return slices;
}

FragmentIdGenerator::FragmentIdGenerator()
    : random_source_(DefaultRandomSource()) {}

FragmentIdGenerator::FragmentIdGenerator(RandomSource random_source)
    : random_source_(random_source ? std::move(random_source)
                                   : DefaultRandomSource()) {}

uint32 FragmentIdGenerator::NextIpv6Id(const IpAddress& destination) {
  auto it = counters_.find(destination);
  if (it == counters_.end()) {
    if (counters_.size() >= kMaxTrackedDestinations && !order_.empty()) {
      counters_.erase(order_.front());
      order_.pop_front();
    }
    uint32 seed = random_source_();
    if (seed == 0) seed = 1;
    order_.push_back(destination);
    it = counters_.emplace(destination, seed).first;
  } else {
    it->second++;
    if (it->second == 0) it->second = 1;
  }
  return it->second;
}

uint16 FragmentIdGenerator::NextIpv4Id(const IpAddress& destination) {
  return static_cast<uint16>(NextIpv6Id(destination) & 0xFFFF);
}

std::optional<std::vector<std::string>> FragmentIpv6Datagram(
    const Ipv6Datagram& datagram, uint16 path_mtu, uint32 identification) {
  size_t unfragmentable_ext =
      datagram.router_alert ? kRouterAlertHeaderSize : 0;
  size_t unfragmentable_total = kIpv6HeaderSize + unfragmentable_ext;

  if (unfragmentable_total + datagram.payload.size() <= path_mtu &&
      unfragmentable_ext + datagram.payload.size() <= kMaxPayloadSize) {
    return std::vector<std::string>{SerializeIpv6Datagram(datagram)};
  }

  size_t headers_per_fragment = unfragmentable_total + kIpv6FragmentHeaderSize;
  if (path_mtu < headers_per_fragment + kFragmentAlignment)
    return std::nullopt;
  size_t max_fragment_payload = path_mtu - headers_per_fragment;

  auto slices = PlanFragments(datagram.payload, max_fragment_payload);
  if (!slices) return std::nullopt;

  std::vector<std::string> packets;
  packets.reserve(slices->size());
  for (const FragmentSlice& slice : *slices) {
    Ipv6Header header;
    header.hop_limit = datagram.hop_limit;
    header.source = datagram.source;
    header.destination = datagram.destination;
    header.next_header =
        datagram.router_alert
            ? static_cast<uint8>(Ipv6NextHeader::HopByHop)
            : static_cast<uint8>(Ipv6NextHeader::Fragment);
    header.payload_length = static_cast<uint16>(
        unfragmentable_ext + kIpv6FragmentHeaderSize + slice.data.size());

    std::string packet;
    packet.reserve(kIpv6HeaderSize + header.payload_length);
    AppendIpv6Header(header, packet);
    WireWriter writer(packet);
    if (datagram.router_alert) {
      writer.WriteU8(static_cast<uint8>(Ipv6NextHeader::Fragment));
      writer.WriteU8(0);
      writer.WriteU8(kOptionRouterAlert);
      writer.WriteU8(kRouterAlertDataLength);
      writer.WriteU16(kRouterAlertMld);
      writer.WriteU8(kOptionPadN);
      writer.WriteU8(0);
    }
    writer.WriteU8(datagram.next_header);
    writer.WriteU8(0);  // Reserved byte.
    uint16 offset_and_flags =
        static_cast<uint16>((slice.offset & 0xFFF8) |
                            (slice.more_fragments ? 1 : 0));
    writer.WriteU16(offset_and_flags);
    writer.WriteU32(identification);
    writer.WriteBytes(slice.data);
    packets.push_back(std::move(packet));
  }
  return packets;
}

std::optional<std::vector<std::string>> FragmentIpv4Datagram(
    const Ipv4Datagram& datagram, uint16 path_mtu, uint16 identification) {
  if (!datagram.source.IsV4() || !datagram.destination.IsV4())
    return std::nullopt;
  if (kIpv4HeaderSize + datagram.payload.size() > kMaxPayloadSize)
    return std::nullopt;

  if (kIpv4HeaderSize + datagram.payload.size() <= path_mtu) {
    uint16 flags = datagram.dont_fragment ? kIpv4DontFragmentFlag : 0;
    return std::vector<std::string>{
        BuildIpv4Packet(datagram, identification, flags, datagram.payload)};
  }

  if (datagram.dont_fragment) return std::nullopt;
  if (path_mtu < kIpv4HeaderSize + kFragmentAlignment) return std::nullopt;

  auto slices = PlanFragments(datagram.payload, path_mtu - kIpv4HeaderSize);
  if (!slices) return std::nullopt;

  std::vector<std::string> packets;
  packets.reserve(slices->size());
  for (const FragmentSlice& slice : *slices) {
    uint16 flags_and_offset =
        static_cast<uint16>((slice.more_fragments ? kIpv4MoreFragmentsFlag : 0) |
                            (slice.offset / kFragmentAlignment));
    packets.push_back(
        BuildIpv4Packet(datagram, identification, flags_and_offset, slice.data));
  }
  return packets;
}
