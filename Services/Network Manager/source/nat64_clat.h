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

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "dns_message.h"
#include "perception/network/ip_address.h"

namespace {

// Domain name queried under RFC 7050 to discover a network's NAT64 prefix.
constexpr std::string_view kIpv4OnlyArpaDomain = "ipv4only.arpa";

// ICMPv6 Neighbor Discovery option type for PREF64 (RFC 8781).
constexpr uint8 kNdOptionTypePref64 = 38;

}  // namespace

// A learned or configured NAT64 prefix (RFC 6052 / RFC 8781 / RFC 7050).
// Valid prefix lengths per RFC 6052 section 2.2 are 32, 40, 48, 56, 64, or 96.
struct Nat64Prefix {
  // The IPv6 prefix address (trailing bits past `prefix_length` are zero).
  ::perception::network::IpAddress prefix;
  // Prefix length in bits (32, 40, 48, 56, 64, or 96).
  uint8 prefix_length = 96;
  // Lifetime in seconds when learned from an RFC 8781 PREF64 RA option.
  uint32 lifetime_seconds = 0;

  // Returns true if `prefix` is V6 and `prefix_length` is a valid RFC 6052
  // length (32, 40, 48, 56, 64, or 96).
  bool IsValid() const;

  bool operator==(const Nat64Prefix& other) const = default;
};

// Returns the RFC 6052 well-known NAT64 prefix 64:ff9b::/96.
Nat64Prefix WellKnownNat64Prefix();

// Returns the local IPv4 CLAT address 192.0.0.4 (RFC 7335 / RFC 6877).
::perception::network::IpAddress ClatLocalIpv4Address();

// Parses a 16-byte RFC 8781 PREF64 Router Advertisement option (including the
// 2-byte type=38, length=2 header). Returns nullopt if malformed or if the
// Prefix Length Code is reserved.
std::optional<Nat64Prefix> ParsePref64RaOption(std::string_view option_bytes);

// Encodes a 16-byte RFC 8781 PREF64 Router Advertisement option. Returns
// nullopt if `nat64_prefix` is not valid.
std::optional<std::string> EncodePref64RaOption(
    const Nat64Prefix& nat64_prefix);

// Discovers a NAT64 prefix from the AAAA answers to an `ipv4only.arpa` query
// (RFC 7050) by matching the well-known IPv4 addresses 192.0.0.170 and
// 192.0.0.171 across all RFC 6052 prefix lengths.
std::optional<Nat64Prefix> DiscoverNat64PrefixFromDns64Answers(
    std::span<const ::perception::network::IpAddress> aaaa_addresses);

// Embeds a 32-bit IPv4 address into `nat64_prefix` per RFC 6052 section 2.2
// (supporting /96, /64, /56, /48, /40, and /32 prefixes, with byte 8 set to 0
// when the prefix is shorter than /96). Returns nullopt if `nat64_prefix` is
// invalid or `ipv4_address` is not V4.
std::optional<::perception::network::IpAddress> SynthesizeIpv6FromIpv4(
    const Nat64Prefix& nat64_prefix,
    const ::perception::network::IpAddress& ipv4_address);

// Extracts the embedded 32-bit IPv4 address from `ipv6_address` if it matches
// `nat64_prefix` per RFC 6052 section 2.2. Returns nullopt if `ipv6_address`
// is not in `nat64_prefix` or `nat64_prefix` is invalid.
std::optional<::perception::network::IpAddress> ExtractIpv4FromSynthesizedIpv6(
    const Nat64Prefix& nat64_prefix,
    const ::perception::network::IpAddress& ipv6_address);

// Performs local DNS64 synthesis (RFC 6147): if `aaaa_records` is non-empty,
// returns it unchanged; otherwise synthesizes AAAA records from `a_records`
// using `nat64_prefix`, preserving each A record's TTL.
std::vector<DnsAddressRecord> SynthesizeDns64Records(
    const Nat64Prefix& nat64_prefix,
    std::span<const DnsAddressRecord> aaaa_records,
    std::span<const DnsAddressRecord> a_records);

// Stateless 464XLAT CLAT translator (RFC 6877 / RFC 7915): translates an
// outgoing IPv4 packet into an IPv6 packet from `clat_ipv6_address` to the
// NAT64-synthesized destination address, translating TCP, UDP, and ICMPv4 Echo
// headers and recomputing transport/ICMPv6 checksums over the IPv6
// pseudo-header. Returns nullopt if the packet is malformed or uses an
// unsupported protocol.
std::optional<std::string> TranslateIpv4ToIpv6(
    std::string_view ipv4_packet,
    const ::perception::network::IpAddress& clat_ipv6_address,
    const Nat64Prefix& nat64_prefix);

// Stateless 464XLAT CLAT translator (RFC 6877 / RFC 7915): translates an
// incoming IPv6 packet addressed to `clat_ipv6_address` from a source inside
// `nat64_prefix` into an IPv4 packet addressed to `clat_ipv4_address`
// (defaulting to 192.0.0.4), translating TCP, UDP, and ICMPv6 Echo headers and
// recomputing IPv4 header and transport/ICMPv4 checksums.
std::optional<std::string> TranslateIpv6ToIpv4(
    std::string_view ipv6_packet,
    const ::perception::network::IpAddress& clat_ipv6_address,
    const Nat64Prefix& nat64_prefix,
    const ::perception::network::IpAddress& clat_ipv4_address =
        ClatLocalIpv4Address());
