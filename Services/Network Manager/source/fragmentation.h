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

#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ipv6_header.h"
#include "perception/network/ip_address.h"

// One 8-byte-aligned slice of an upper-layer payload produced by source
// fragmentation (shared between IPv4 and IPv6).
struct FragmentSlice {
  // Byte offset from the start of the fragmentable payload (multiple of 8).
  uint16 offset = 0;
  // True if additional fragments follow this slice.
  bool more_fragments = false;
  // View of the slice bytes within the original payload.
  std::string_view data;
};

// Splits `payload` into fragments that each hold at most
// `max_fragment_payload` bytes, rounding non-final fragments down to a
// multiple of 8 bytes. Returns nullopt if `payload` exceeds 65535 bytes or if
// fragmentation is required and `max_fragment_payload < 8`.
std::optional<std::vector<FragmentSlice>> PlanFragments(
    std::string_view payload, size_t max_fragment_payload);

// Per-destination fragment Identification generator (RFC 7739 section 5.3).
// Seeds each destination's counter from an injected random source and
// increments it per fragmented datagram.
class FragmentIdGenerator {
 public:
  // Random 32-bit generator callback.
  using RandomSource = std::function<uint32()>;

  // Uses a default xorshift generator.
  FragmentIdGenerator();

  explicit FragmentIdGenerator(RandomSource random_source);

  // Returns the next non-zero 32-bit IPv6 Fragment Identification for
  // `destination`.
  uint32 NextIpv6Id(const ::perception::network::IpAddress& destination);

  // Returns the next 16-bit IPv4 Identification for `destination`.
  uint16 NextIpv4Id(const ::perception::network::IpAddress& destination);

 private:
  // Source of initial random counter values.
  RandomSource random_source_;
  // Per-destination 32-bit counters.
  std::map<::perception::network::IpAddress, uint32> counters_;
  // Insertion order used for FIFO eviction when the table is full.
  std::deque<::perception::network::IpAddress> order_;
};

// Serializes `datagram` into one IPv6 packet if it fits within `path_mtu`, or
// into 8-byte-aligned IPv6 fragments carrying Fragment extension headers with
// `identification` when it exceeds `path_mtu`. Returns nullopt if `path_mtu`
// is too small to hold the unfragmentable headers plus 8 bytes of payload.
std::optional<std::vector<std::string>> FragmentIpv6Datagram(
    const Ipv6Datagram& datagram, uint16 path_mtu, uint32 identification);

// An outgoing IPv4 datagram before header serialization and optional source
// fragmentation.
struct Ipv4Datagram {
  // Source IPv4 address.
  ::perception::network::IpAddress source;
  // Destination IPv4 address.
  ::perception::network::IpAddress destination;
  // Upper-layer protocol (e.g. 6 = TCP, 17 = UDP, 1 = ICMPv4).
  uint8 protocol = 0;
  // Time to live.
  uint8 ttl = 64;
  // DSCP and ECN byte.
  uint8 dscp_ecn = 0;
  // Don't Fragment (DF) flag; when true, fragmentation above `path_mtu` fails.
  bool dont_fragment = false;
  // Upper-layer payload.
  std::string payload;
};

// Serializes `datagram` into one IPv4 packet if it fits within `path_mtu`, or
// into 8-byte-aligned IPv4 fragments with `identification` when it exceeds
// `path_mtu` and `dont_fragment` is false. Returns nullopt if fragmentation is
// required while `dont_fragment` is true, or if `path_mtu` cannot hold the
// 20-byte header plus 8 bytes of payload.
std::optional<std::vector<std::string>> FragmentIpv4Datagram(
    const Ipv4Datagram& datagram, uint16 path_mtu, uint16 identification);
