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

#include "reassembly.h"

#include <algorithm>

using ::perception::network::IpAddressFamily;

namespace {

// Fragment offsets and non-final fragment sizes must be multiples of 8 bytes.
constexpr size_t kFragmentAlignment = 8;

// Maximum reassembled payload size (64 KiB - 1, RFC 791 / RFC 8200).
constexpr size_t kMaxDatagramSize = 65535;

// Maximum number of datagrams simultaneously awaiting reassembly.
constexpr size_t kMaxConcurrentDatagrams = 32;

// Reassembly timeout for IPv4 datagrams in seconds.
constexpr int kIpv4TimeoutSeconds = 30;

// Reassembly timeout for IPv6 datagrams in seconds (RFC 8200 section 4.5).
constexpr int kIpv6TimeoutSeconds = 60;

// IPv6 Next Header value for Hop-by-Hop Options.
constexpr uint8 kNextHeaderHopByHop = 0;

// IPv6 Next Header value for TCP.
constexpr uint8 kNextHeaderTcp = 6;

// IPv6 Next Header value for UDP.
constexpr uint8 kNextHeaderUdp = 17;

// IPv6 Next Header value for Routing header.
constexpr uint8 kNextHeaderRouting = 43;

// IPv6 Next Header value for Fragment header.
constexpr uint8 kNextHeaderFragment = 44;

// IPv6 Next Header value for ICMPv6.
constexpr uint8 kNextHeaderIcmpv6 = 58;

// IPv6 Next Header value for No Next Header.
constexpr uint8 kNextHeaderNone = 59;

// IPv6 Next Header value for Destination Options.
constexpr uint8 kNextHeaderDestinationOptions = 60;

// Minimum TCP header size in bytes.
constexpr size_t kMinTcpHeaderSize = 20;

// Minimum UDP header size in bytes.
constexpr size_t kMinUdpHeaderSize = 8;

// Minimum ICMPv6 header size in bytes.
constexpr size_t kMinIcmpv6HeaderSize = 8;

// First Neighbor Discovery ICMPv6 message type (Router Solicitation, 133).
constexpr uint8 kNdpTypeFirst = 133;

// Last Neighbor Discovery ICMPv6 message type (Redirect, 137).
constexpr uint8 kNdpTypeLast = 137;

// Validates RFC 7112 (the first IPv6 fragment must hold the complete header
// chain through the upper-layer header) and RFC 6980 (NDP messages must never
// be fragmented, including atomic fragments).
bool ValidateIpv6FirstFragment(uint8 initial_next_header, std::string_view data,
                               bool more_fragments) {
  uint8 next_header = initial_next_header;
  size_t offset = 0;

  while (true) {
    if (next_header == kNextHeaderHopByHop ||
        next_header == kNextHeaderRouting ||
        next_header == kNextHeaderDestinationOptions) {
      if (offset + 2 > data.size()) return false;
      size_t ext_len =
          (static_cast<uint8>(data[offset + 1]) + 1) * kFragmentAlignment;
      if (offset + ext_len > data.size()) return false;
      next_header = static_cast<uint8>(data[offset]);
      offset += ext_len;
      continue;
    }
    if (next_header == kNextHeaderFragment) return false;
    break;
  }

  // RFC 6980 forbids NDP inside any fragment (including atomic fragments).
  if (next_header == kNextHeaderIcmpv6) {
    if (offset + 1 <= data.size()) {
      uint8 icmp_type = static_cast<uint8>(data[offset]);
      if (icmp_type >= kNdpTypeFirst && icmp_type <= kNdpTypeLast) return false;
    }
    if (more_fragments && offset + kMinIcmpv6HeaderSize > data.size())
      return false;
    return true;
  }

  if (!more_fragments) return true;

  if (next_header == kNextHeaderTcp) {
    if (offset + kMinTcpHeaderSize > data.size()) return false;
    uint8 data_offset_words = static_cast<uint8>(data[offset + 12]) >> 4;
    size_t tcp_header_size = static_cast<size_t>(data_offset_words) * 4;
    return tcp_header_size >= kMinTcpHeaderSize &&
           offset + tcp_header_size <= data.size();
  }
  if (next_header == kNextHeaderUdp)
    return offset + kMinUdpHeaderSize <= data.size();
  if (next_header == kNextHeaderNone) return false;

  return offset < data.size();
}

}  // namespace

ReassemblyResult Reassembler::AddFragment(
    const ReassemblyKey& key, uint16 offset, bool more_fragments,
    std::string_view data, std::chrono::steady_clock::time_point now,
    size_t unfragmentable_length) {
  if (key.family != IpAddressFamily::V4 && key.family != IpAddressFamily::V6)
    return {ReassemblyStatus::InvalidFragment, 0, {}};
  if (offset % kFragmentAlignment != 0)
    return {ReassemblyStatus::InvalidFragment, 0, {}};

  if (key.family == IpAddressFamily::V6 && offset == 0) {
    if (!ValidateIpv6FirstFragment(key.protocol, data, more_fragments)) {
      buffers_.erase(key);
      return {ReassemblyStatus::IncompleteFirstFragmentHeader, 0, {}};
    }
  }

  // Atomic fragment (RFC 6946): offset 0 and M=0 is a self-contained packet
  // that must not collide with or discard any in-progress reassembly buffer.
  if (offset == 0 && !more_fragments) {
    if (unfragmentable_length + data.size() > kMaxDatagramSize)
      return {ReassemblyStatus::DatagramTooLarge, 0, {}};
    return {ReassemblyStatus::Complete, key.protocol, std::string(data)};
  }

  if (data.empty()) return {ReassemblyStatus::InvalidFragment, 0, {}};
  if (more_fragments && data.size() % kFragmentAlignment != 0)
    return {ReassemblyStatus::InvalidFragment, 0, {}};

  size_t fragment_end = static_cast<size_t>(offset) + data.size();
  if (unfragmentable_length + fragment_end > kMaxDatagramSize) {
    buffers_.erase(key);
    return {ReassemblyStatus::DatagramTooLarge, 0, {}};
  }

  auto it = buffers_.find(key);
  if (it != buffers_.end() && it->second.expires <= now) {
    buffers_.erase(it);
    it = buffers_.end();
  }

  if (it == buffers_.end()) {
    if (buffers_.size() >= kMaxConcurrentDatagrams) MakeRoom(now);
    auto timeout = std::chrono::seconds(
        key.family == IpAddressFamily::V6 ? kIpv6TimeoutSeconds
                                          : kIpv4TimeoutSeconds);
    Buffer fresh;
    fresh.expires = now + timeout;
    fresh.first_fragment_protocol = key.protocol;
    it = buffers_.emplace(key, std::move(fresh)).first;
  }

  Buffer& buffer = it->second;
  if (offset == 0) {
    buffer.unfragmentable_length = unfragmentable_length;
    buffer.first_fragment_protocol = key.protocol;
  }
  if (buffer.total_length.has_value() &&
      buffer.unfragmentable_length + *buffer.total_length > kMaxDatagramSize) {
    buffers_.erase(it);
    return {ReassemblyStatus::DatagramTooLarge, 0, {}};
  }

  // RFC 5722: any overlapping fragment or conflicting total length silently
  // drops the entire datagram being reassembled.
  for (const Piece& piece : buffer.pieces) {
    size_t piece_start = piece.offset;
    size_t piece_end = piece_start + piece.data.size();
    if (offset < piece_end && piece_start < fragment_end) {
      buffers_.erase(it);
      return {ReassemblyStatus::DroppedOverlap, 0, {}};
    }
  }

  if (!more_fragments) {
    if (buffer.total_length.has_value() ||
        (!buffer.pieces.empty() &&
         buffer.pieces.back().offset + buffer.pieces.back().data.size() >
             fragment_end)) {
      buffers_.erase(it);
      return {ReassemblyStatus::DroppedOverlap, 0, {}};
    }
    if (buffer.unfragmentable_length + fragment_end > kMaxDatagramSize) {
      buffers_.erase(it);
      return {ReassemblyStatus::DatagramTooLarge, 0, {}};
    }
    buffer.total_length = fragment_end;
  } else if (buffer.total_length.has_value() &&
             fragment_end >= *buffer.total_length) {
    buffers_.erase(it);
    return {ReassemblyStatus::DroppedOverlap, 0, {}};
  }

  if (offset == 0) {
    buffer.has_first_fragment = true;
    buffer.first_fragment_payload.assign(data);
  }

  auto insert_pos = std::lower_bound(
      buffer.pieces.begin(), buffer.pieces.end(), offset,
      [](const Piece& piece, uint16 target) { return piece.offset < target; });
  buffer.pieces.insert(insert_pos, Piece{offset, std::string(data)});
  buffer.received_bytes += data.size();

  if (buffer.total_length.has_value() &&
      buffer.received_bytes == *buffer.total_length) {
    std::string payload;
    payload.reserve(*buffer.total_length);
    for (const Piece& piece : buffer.pieces) payload.append(piece.data);
    uint8 protocol = buffer.first_fragment_protocol;
    buffers_.erase(it);
    return {ReassemblyStatus::Complete, protocol, std::move(payload)};
  }

  return {ReassemblyStatus::Incomplete, buffer.first_fragment_protocol, {}};
}

std::vector<ExpiredReassembly> Reassembler::Purge(
    std::chrono::steady_clock::time_point now) {
  std::vector<ExpiredReassembly> expired;
  for (auto it = buffers_.begin(); it != buffers_.end();) {
    if (it->second.expires <= now) {
      expired.push_back({it->first, it->second.has_first_fragment,
                         std::move(it->second.first_fragment_payload)});
      it = buffers_.erase(it);
    } else {
      ++it;
    }
  }
  return expired;
}

void Reassembler::MakeRoom(std::chrono::steady_clock::time_point now) {
  Purge(now);
  if (buffers_.size() < kMaxConcurrentDatagrams) return;
  auto oldest = std::min_element(
      buffers_.begin(), buffers_.end(), [](const auto& a, const auto& b) {
        return a.second.expires < b.second.expires;
      });
  buffers_.erase(oldest);
}
